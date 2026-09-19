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

std::vector<OnlineResult> JellyfinSource::search(const std::string& query, int count, std::string* error_out,
                                                 OnlineSearchScope scope) {
    if (!configured()) {
        if (error_out) *error_out = "Jellyfin server not configured (set JellyfinServerUrl + JellyfinApiKey in config.txt)";
        return {};
    }

    // A bare /p: (empty text) means "list every playlist" — reuse the
    // exact request the add-to-playlist picker uses so the two views can
    // never disagree about which playlists exist.
    if (scope == OnlineSearchScope::Playlist && query.empty()) {
        return list_playlists(error_out);
    }

    const char* scoped_type = nullptr;
    switch (scope) {
        case OnlineSearchScope::Playlist: scoped_type = "Playlist"; break;
        case OnlineSearchScope::Artist:   scoped_type = "Artist"; break;   // raw Person hints dropped below
        case OnlineSearchScope::Album:    scoped_type = "MusicAlbum"; break; // "Album" is not a valid hint type on this server
        case OnlineSearchScope::All:      break;
    }

    std::vector<OnlineResult> results;

    if (scoped_type) {
        // Single-type request only — the scope is enforced server-side by
        // includeItemTypes, so no other type can leak into the results.
        std::string url = base_url_ + "/Search/Hints?searchTerm=" + url_encode(query) +
                          "&includeItemTypes=" + scoped_type + "&limit=" + std::to_string(count);
        std::string body, err;
        if (!http_get_json(url, auth_header(), insecure_, body, err)) {
            if (error_out) *error_out = err;
            return {};
        }
        minijson::Value root;
        if (!minijson::parse(body, root)) {
            if (error_out) *error_out = "server returned unparseable search results";
            return {};
        }
        const minijson::Value* hints = root.get("SearchHints");
        if (!hints || !hints->is_array()) return results;

        const char* want = (scope == OnlineSearchScope::Artist) ? "MusicArtist" : scoped_type;
        for (const auto& hint : hints->arr) {
            if (sget(&hint, "Type") != want) continue;
            OnlineResult item;
            item.video_id = sget(&hint, "Id");
            item.title = sget(&hint, "Name");
            item.uploader = artist_from_hint(hint);
            item.type = want;
            if (!item.video_id.empty() && !item.title.empty()) results.push_back(std::move(item));
        }
        return results;
    }

    // 1) Tracks + containers in one request. Albums are "MusicAlbum" hints
    //    (the bare "Album" type isn't a valid Search/Hints value on this
    //    server and silently yields zero album hits).
    std::string url = base_url_ + "/Search/Hints?searchTerm=" + url_encode(query) +
                      "&includeItemTypes=Audio,MusicAlbum,Playlist&limit=" + std::to_string(count);
    std::string body, err;
    if (http_get_json(url, auth_header(), insecure_, body, err)) {
        minijson::Value root;
        if (minijson::parse(body, root)) {
            const minijson::Value* hints = root.get("SearchHints");
            if (hints && hints->is_array()) {
                for (const auto& hint : hints->arr) {
                    std::string type = sget(&hint, "Type");
                    if (type != "Audio" && type != "Album" && type != "MusicAlbum" && type != "Playlist") continue;

                    OnlineResult item;
                    item.video_id = sget(&hint, "Id");
                    item.title = sget(&hint, "Name");
                    item.uploader = artist_from_hint(hint);
                    item.type = type;
                    if (!item.video_id.empty() && !item.title.empty()) results.push_back(std::move(item));
                }
            }
        }
    } else if (error_out && results.empty()) {
        *error_out = err;
    }

    // 2) Music artists via their own request — the server only returns
    //    artist hits when Artist is the sole includeItemTypes value.
    std::string artist_url = base_url_ + "/Search/Hints?searchTerm=" + url_encode(query) +
                             "&includeItemTypes=Artist&limit=" + std::to_string(count);
    std::string abody, aerr;
    if (http_get_json(artist_url, auth_header(), insecure_, abody, aerr)) {
        minijson::Value aroot;
        if (minijson::parse(abody, aroot)) {
            const minijson::Value* hints = aroot.get("SearchHints");
            if (hints && hints->is_array()) {
                for (const auto& hint : hints->arr) {
                    if (sget(&hint, "Type") != "MusicArtist") continue; // drop raw Person people

                    OnlineResult item;
                    item.video_id = sget(&hint, "Id");
                    item.title = sget(&hint, "Name");
                    item.type = "MusicArtist";
                    if (!item.video_id.empty() && !item.title.empty()) results.push_back(std::move(item));
                }
            }
        }
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

std::string JellyfinSource::resolve_playlist_user_id() {
    if (!playlist_user_id_.empty() || !configured()) return playlist_user_id_;

    // The static API key is a system key with no bound user (/Users/Me is
    // null), but playlists are owned by real accounts — and playlist
    // mutations are only accepted when the acting UserId belongs to the
    // owner (or an administrator). Prefer the first Administrator, since
    // they can mutate every playlist; fall back to the first user and stop.
    std::string url = base_url_ + "/Users";
    std::string body, err;
    if (!http_get_json(url, auth_header(), insecure_, body, err)) return {};

    minijson::Value root;
    if (!minijson::parse(body, root) || !root.is_array() || root.arr.empty()) return {};

    const minijson::Value* fallback = nullptr;
    for (const auto& u : root.arr) {
        if (!u.is_object()) continue;
        const minijson::Value* id = u.get("Id");
        if (!id) continue;
        if (fallback == nullptr) fallback = &u;
        const minijson::Value* policy = u.get("Policy");
        const minijson::Value* is_admin = policy ? policy->get("IsAdministrator") : nullptr;
        if (is_admin && is_admin->type == minijson::Type::Bool && is_admin->b) {
            playlist_user_id_ = id->as_string();
            return playlist_user_id_;
        }
    }
    if (fallback) playlist_user_id_ = fallback->get("Id")->as_string();
    return playlist_user_id_;
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

std::vector<OnlineResult> JellyfinSource::list_artist_tracks(const std::string& artist_id, std::string* error_out) {
    if (!configured()) {
        if (error_out) *error_out = "Jellyfin server not configured";
        return {};
    }
    if (artist_id.empty()) {
        if (error_out) *error_out = "empty artist id";
        return {};
    }

    std::string url = base_url_ + "/Items?ArtistIds=" + url_encode(artist_id) +
                      "&IncludeItemTypes=Audio&Recursive=true";
    std::string uid = resolve_user_id();
    if (!uid.empty()) url += "&UserId=" + url_encode(uid);

    std::string body, err;
    if (!http_get_json(url, auth_header(), insecure_, body, err)) {
        if (error_out) *error_out = err;
        return {};
    }

    minijson::Value root;
    if (!minijson::parse(body, root) || !root.is_object()) {
        if (error_out) *error_out = "server returned unparseable artist list";
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
// Whole-library "recently added" listing
// =====================================================================

std::vector<OnlineResult> JellyfinSource::list_recent(int limit, int start_index,
                                                       int* total_out, std::string* error_out) {
    if (total_out) *total_out = 0;
    if (!configured()) {
        if (error_out) *error_out = "Jellyfin server not configured";
        return {};
    }

    if (start_index < 0) start_index = 0;
    if (limit < 1) limit = 1;

    std::string uid = resolve_user_id();
    std::string url = base_url_ + "/Items?IncludeItemTypes=Audio&Recursive=true"
                      "&SortBy=DateCreated&SortOrder=Descending"
                      "&StartIndex=" + std::to_string(start_index) +
                      "&Limit=" + std::to_string(limit);
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

    if (total_out) {
        const minijson::Value* total = root.get("TotalRecordCount");
        if (total) *total_out = static_cast<int>(total->as_number());
    }

    const minijson::Value* items = root.get("Items");
    if (!items || !items->is_array()) return {};

    std::vector<OnlineResult> results;
    results.reserve(items->arr.size());
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
// Playlists
// =====================================================================

std::vector<OnlineResult> JellyfinSource::list_playlists(std::string* error_out) {
    if (!configured()) {
        if (error_out) *error_out = "Jellyfin server not configured";
        return {};
    }

    // Every playlist the API key can see (the app runs as an admin key, so
    // without a UserId filter this returns ALL playlists across users —
    // Albums/collections created from playlists don't get collapsed into
    // the owned-only view the way a user-scoped query does). Returns the
    // containers as rows; like search results, they're Type "Playlist".
    std::string url = base_url_ + "/Items?IncludeItemTypes=Playlist&Recursive=true&Limit=500";
    std::string body, err;
    if (!http_get_json(url, auth_header(), insecure_, body, err)) {
        if (error_out) *error_out = err;
        return {};
    }

    minijson::Value root;
    if (!minijson::parse(body, root) || !root.is_object()) {
        if (error_out) *error_out = "server returned unparseable playlist list";
        return {};
    }

    const minijson::Value* items = root.get("Items");
    if (!items || !items->is_array()) return {};

    std::vector<OnlineResult> results;
    results.reserve(items->arr.size());
    for (const auto& it : items->arr) {
        if (sget(&it, "Type") != "Playlist") continue;
        OnlineResult item;
        item.video_id = sget(&it, "Id");
        item.title = sget(&it, "Name");
        item.uploader = "playlist";
        item.type = "Playlist";
        if (item.video_id.empty() || item.title.empty()) continue;
        results.push_back(std::move(item));
    }
    return results;
}

bool JellyfinSource::add_to_playlist(const std::string& playlist_id, const std::string& item_id,
                                     std::string* error_out) {
    if (!configured()) {
        if (error_out) *error_out = "Jellyfin server not configured";
        return false;
    }
    if (playlist_id.empty() || item_id.empty()) {
        if (error_out) *error_out = "empty playlist or item id";
        return false;
    }

    // POST /Playlists/{playlist_id}/Items?Ids=<item_id>. Server-side this
    // auto-expands container ids (Album/MusicArtist/Playlist) into their
    // Audio children, so sending one hovered item id adds the whole album,
    // or just the single track when it's an Audio item. 204 = success.
    // The server 400s "Error processing request" without the acting user:
    // the static API key has no user bound, so the admin's id (the playlist
    // owner context) has to ride along explicitly.
    std::string uid = resolve_playlist_user_id();
    std::string url = base_url_ + "/Playlists/" + url_encode(playlist_id) +
                      "/Items?Ids=" + url_encode(item_id);
    if (!uid.empty()) url += "&UserId=" + url_encode(uid);
    std::string body, err;
    if (!http_raw(url, auth_header(), insecure_, "POST", "", body, err)) {
        if (error_out) *error_out = err;
        return false;
    }
    return true;
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