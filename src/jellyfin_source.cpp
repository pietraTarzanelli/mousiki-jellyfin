#include "jellyfin_source.h"
#include "http_util.h"
#include "minijson.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <system_error>

namespace muisc {

namespace {

// Read a string member ("" if missing/not a string).
std::string sget(const minijson::Value* v, const char* key) {
    const minijson::Value* m = v ? v->get(key) : nullptr;
    return m ? m->as_string() : std::string();
}

// First artist from the Artists[] array, else AlbumArtist, else the raw
// album-artist convenience alias.
std::string artist_from_hint(const minijson::Value& object) {
    if (const minijson::Value* artists = object.get("Artists")) {
        if (!artists->arr.empty()) return artists->arr[0].as_string();
    }
    std::string album_artist = sget(&object, "AlbumArtist");
    if (!album_artist.empty()) return album_artist;
    return sget(&object, "ArtistsDetail");
}

// Ticks (100ns units) -> seconds.
double ticks_to_sec(double ticks) { return ticks / 10000000.0; }

// Formats [MM:SS.CC] / <MM:SS.CC> LRC timestamps from seconds.
std::string fmt_lrc_ts(double seconds) {
    if (seconds < 0) seconds = 0;
    int mm = static_cast<int>(seconds) / 60;
    int ss = static_cast<int>(seconds) % 60;
    int cs = static_cast<int>(std::floor((seconds - std::floor(seconds)) * 100.0 + 0.5));
    if (cs >= 100) { cs = 0; ++ss; if (ss == 60) { ss = 0; ++mm; } }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d.%02d", mm, ss, cs);
    return buf;
}

} // namespace

// =====================================================================
// Configuration
// =====================================================================

void JellyfinSource::configure(std::string base_url, std::string api_key, bool insecure) {
    while (!base_url.empty() && base_url.back() == '/') base_url.pop_back();
    base_url_ = std::move(base_url);
    api_key_ = std::move(api_key);
    insecure_ = insecure;
}

bool JellyfinSource::configured() const {
    return !base_url_.empty() && !api_key_.empty();
}

std::string JellyfinSource::auth_header() const {
    // curl receives this whole string as one -H argument via shell_quote,
    // so the embedded double quotes survive literally.
    return "Authorization: MediaBrowser Token=\"" + api_key_ + "\"";
}

// =====================================================================
// Search
// =====================================================================

std::vector<OnlineResult> JellyfinSource::search(const std::string& query, int count, std::string* error_out) {
    if (!configured()) {
        if (error_out) *error_out = "Jellyfin server not configured (set JellyfinServerUrl + JellyfinApiKey in config.txt)";
        return {};
    }

    std::string url = base_url_ + "/Search/Hints?searchTerm=" + url_encode(query) +
                      "&includeItemTypes=Audio,Album,Playlist&limit=" + std::to_string(count);
    std::string body, err;
    if (!http_get_json(url, auth_header(), insecure_, body, err)) {
        if (error_out) *error_out = err;
        return {};
    }

    minijson::Value root;
    if (!minijson::parse(body, root)) {
        if (error_out) *error_out = "server returned unparseable JSON";
        return {};
    }

    const minijson::Value* hints = root.get("SearchHints");
    if (!hints || !hints->is_array()) return {};

    std::vector<OnlineResult> results;
    for (const auto& hint : hints->arr) {
        std::string type = sget(&hint, "Type");
        if (type != "Audio" && type != "Album" && type != "Playlist") continue; // skip artist/api/program hints

        OnlineResult item;
        item.video_id = sget(&hint, "Id");
        item.title = sget(&hint, "Name");
        item.uploader = artist_from_hint(hint);
        item.type = type;
        if (item.video_id.empty() || item.title.empty()) continue;
        results.push_back(std::move(item));
    }
    return results;
}

// =====================================================================
// Container browsing (album / playlist -> contained tracks)
// =====================================================================

std::string JellyfinSource::resolve_user_id() {
    if (!user_id_.empty() || !configured()) return user_id_;

    std::string url = base_url_ + "/Users";
    std::string body, err;
    if (!http_get_json(url, auth_header(), insecure_, body, err)) return {};

    minijson::Value root;
    if (!minijson::parse(body, root) || !root.is_array() || root.arr.empty()) return {};

    user_id_ = root.arr[0].get("Id") ? root.arr[0].get("Id")->as_string() : std::string();
    return user_id_;
}

std::vector<OnlineResult> JellyfinSource::list_children(const std::string& parent_id, std::string* error_out) {
    if (!configured()) {
        if (error_out) *error_out = "Jellyfin server not configured";
        return {};
    }
    if (parent_id.empty()) {
        if (error_out) *error_out = "empty item id";
        return {};
    }

    std::string url = base_url_ + "/Items?ParentId=" + url_encode(parent_id) +
                      "&IncludeItemTypes=Audio&Recursive=false";
    std::string uid = resolve_user_id();
    if (!uid.empty()) url += "&UserId=" + url_encode(uid);

    std::string body, err;
    if (!http_get_json(url, auth_header(), insecure_, body, err)) {
        if (error_out) *error_out = err;
        return {};
    }

    minijson::Value root;
    if (!minijson::parse(body, root) || !root.is_object()) {
        if (error_out) *error_out = "server returned unparseable item list";
        return {};
    }

    const minijson::Value* items = root.get("Items");
    if (!items || !items->is_array()) return {};

    std::vector<OnlineResult> results;
    for (const auto& it : items->arr) {
        if (sget(&it, "Type") != "Audio") continue;
        OnlineResult item;
        item.video_id = sget(&it, "Id");
        item.title = sget(&it, "Name");
        item.uploader = artist_from_hint(it);
        item.type = "Audio";
        if (item.video_id.empty() || item.title.empty()) continue;
        results.push_back(std::move(item));
    }
    return results;
}

// =====================================================================
// Resolve + download
// =====================================================================

std::optional<SongResult> JellyfinSource::resolve_by_id(const std::string& item_id, const std::string& title,
                                                        const std::string& artist, std::string* error_out) {
    if (!configured()) {
        if (error_out) *error_out = "Jellyfin server not configured";
        return std::nullopt;
    }
    if (item_id.empty()) {
        if (error_out) *error_out = "empty Jellyfin item id";
        return std::nullopt;
    }

    // 1) Read item metadata (container/ext + server-side name/artist).
    //    Note: some servers 400 on the canonical /Items/{id} route for an
    //    API key, but accept the batch form /Items?ids=<id> — use that;
    //    the result is a single-item list.
    std::string url = base_url_ + "/Items?ids=" + url_encode(item_id);
    std::string body, err;
    if (!http_get_json(url, auth_header(), insecure_, body, err)) {
        if (error_out) *error_out = "metadata fetch failed: " + err;
        return std::nullopt;
    }
    minijson::Value root;
    if (!minijson::parse(body, root) || !root.is_object()) {
        if (error_out) *error_out = "server returned unparseable item JSON";
        return std::nullopt;
    }
    const minijson::Value* items = root.get("Items");
    if (!items || !items->is_array() || items->arr.empty()) {
        if (error_out) *error_out = "item not found on server";
        return std::nullopt;
    }
    const minijson::Value& item = items->arr[0];

    std::string container = sget(&item, "Container");
    if (container.empty()) container = "mp3"; // decode doesn't depend on the extension
    std::string server_title = sget(&item, "Name");
    if (server_title.empty()) server_title = title;
    std::string server_artist = artist_from_hint(item);
    if (server_artist.empty()) server_artist = artist;

    // 2) Download to cache if not already there. The cache key mixes the
    //    item id with the title so equal titles on different albums can't
    //    collide and the cached copy doesn't depend on server title changes.
    std::string key = "jf" + item_id + "-" + server_title;
    std::string ext;
    for (char c : container) ext += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    fs::path dest = cache_.path_for(key, ext);
    if (cache_.is_cached(key, ext)) {
        return SongResult{server_title, server_artist, dest, true};
    }

    std::string stream_url = base_url_ + "/Audio/" + url_encode(item_id) + "/stream" + "?static=true";
    if (!http_download_to_file(stream_url, auth_header(), insecure_, dest.string(), err)) {
        if (error_out) *error_out = err;
        return std::nullopt;
    }

    return SongResult{server_title, server_artist, dest, false};
}

// =====================================================================
// Lyrics
// =====================================================================

std::optional<std::string> JellyfinSource::fetch_lyrics_lrc(const std::string& item_id, std::string* error_out) {
    if (!configured()) return std::nullopt;

    std::string url = base_url_ + "/Audio/" + url_encode(item_id) + "/Lyrics";
    std::string body, err;
    if (!http_get_json(url, auth_header(), insecure_, body, err)) {
        // Jellyfin answers 404 for tracks without stored lyrics — that's
        // a normal "no lyrics", not a connection problem.
        if (error_out) *error_out = "server has no lyrics for this track";
        return std::nullopt;
    }

    minijson::Value root;
    if (!minijson::parse(body, root) || !root.is_object()) {
        if (error_out) *error_out = "server returned unparseable lyrics JSON";
        return std::nullopt;
    }

    const minijson::Value* lines = root.get("Lyrics");
    if (!lines || !lines->is_array() || lines->arr.empty()) {
        if (error_out) *error_out = "server has no lyrics for this track";
        return std::nullopt;
    }

    // Line-synced content: each entry is {"Start": <ticks>, "Text": ".."}.
    std::vector<double> line_starts;
    std::vector<std::string> line_texts;
    for (const auto& line : lines->arr) {
        if (!line.is_object()) continue;
        const minijson::Value* start = line.get("Start");
        line_starts.push_back(start ? ticks_to_sec(start->as_number()) : 0.0);
        line_texts.push_back(sget(&line, "Text"));
    }
    if (line_texts.empty()) {
        if (error_out) *error_out = "server has no lyrics for this track";
        return std::nullopt;
    }

    // Optional ELRC word cues: each cue {Line, Position, Start} marks the
    // char offset where a word begins on that line; word k spans
    // [pos_k, pos_{k+1}) and starts at cue[k].Start (in ticks).
    std::vector<std::vector<std::pair<double, std::string>>> words_by_line(line_texts.size());
    const minijson::Value* cues = root.get("Cues");
    if (cues && cues->is_array()) {
        struct WordMark { size_t pos; double t; };
        std::vector<std::vector<WordMark>> marks(line_texts.size());
        for (const auto& cue : cues->arr) {
            if (!cue.is_object()) continue;
            const minijson::Value* lv = cue.get("Line");
            const minijson::Value* pv = cue.get("Position");
            const minijson::Value* tv = cue.get("Start");
            if (!lv || !pv || !tv) continue;
            int li = static_cast<int>(lv->as_number());
            if (li < 0 || li >= static_cast<int>(line_texts.size())) continue;
            marks[li].push_back({static_cast<size_t>(pv->as_number()), ticks_to_sec(tv->as_number())});
        }
        for (size_t li = 0; li < marks.size(); ++li) {
            auto& m = marks[li];
            std::sort(m.begin(), m.end(), [](const WordMark& a, const WordMark& b) { return a.pos < b.pos; });
            const std::string& text = line_texts[li];
            for (size_t k = 0; k < m.size(); ++k) {
                size_t end = (k + 1 < m.size()) ? m[k + 1].pos : text.size();
                if (m[k].pos > text.size()) break;
                std::string word = text.substr(m[k].pos, end - m[k].pos);
                while (!word.empty() && std::isspace(static_cast<unsigned char>(word.front()))) word.erase(word.begin());
                while (!word.empty() && std::isspace(static_cast<unsigned char>(word.back()))) word.pop_back();
                if (!word.empty()) words_by_line[li].emplace_back(m[k].t, word);
            }
        }
    }

    // Emit LRC: word-synced lines get enhanced-LRC word timestamps, the
    // rest get plain line timestamps.
    std::ostringstream lrc;
    for (size_t i = 0; i < line_texts.size(); ++i) {
        lrc << "[" << fmt_lrc_ts(line_starts[i]) << "]";
        if (!words_by_line[i].empty()) {
            for (size_t k = 0; k < words_by_line[i].size(); ++k) {
                lrc << "<" << fmt_lrc_ts(words_by_line[i][k].first) << ">"
                    << words_by_line[i][k].second;
                if (k + 1 < words_by_line[i].size()) lrc << " ";
            }
        } else {
            lrc << line_texts[i];
        }
        lrc << "\n";
    }
    return lrc.str();
}

} // namespace muisc