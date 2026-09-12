#include "lyrics_fetcher.h"
#include "TextSanitizer.h"
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>

namespace muisc {

// Sidecar lyrics file lives next to the track, same stem, .lrc extension —
// e.g. "Song Title.flac" -> "Song Title.lrc". Works for both a user's own
// library and the Jellyfin cache dir (both are "the music folder" for
// whatever track lives there), and is what lets a previously-fetched
// track show lyrics offline.
static fs::path sidecar_path(const fs::path& track_path) {
    if (track_path.empty()) return {};
    return track_path.parent_path() / (track_path.stem().string() + ".lrc");
}

static bool load_sidecar(const fs::path& track_path, std::string& out_lrc) {
    fs::path p = sidecar_path(track_path);
    if (p.empty()) return false;
    std::error_code ec;
    if (!fs::exists(p, ec)) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream oss;
    oss << in.rdbuf();
    out_lrc = oss.str();
    return !out_lrc.empty();
}

static void save_sidecar(const fs::path& track_path, const std::string& lrc) {
    fs::path p = sidecar_path(track_path);
    if (p.empty() || lrc.empty()) return;
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (out.is_open()) out << lrc;
}

// --- enhanced/plain LRC parsing ----------------------------------------

static std::vector<LyricLine> parse_lrc(const std::string& lrc_text) {
    std::string sanitized_lrc = sanitize_lyric_text(lrc_text);
    std::vector<LyricLine> lines;
    std::istringstream stream(sanitized_lrc);
    std::string raw_line;

    static const std::regex line_ts_re(R"(^\[(\d+):(\d+(?:\.\d+)?)\])");
    static const std::regex word_ts_re(R"(<(\d+):(\d+(?:\.\d+)?)>)");

    while (std::getline(stream, raw_line)) {
        if (!raw_line.empty() && raw_line.back() == '\r') raw_line.pop_back();

        std::smatch m;
        if (!std::regex_search(raw_line, m, line_ts_re)) continue; // skip metadata/[ar:]/[ti:] tags etc.

        double line_time = std::stod(m[1].str()) * 60.0 + std::stod(m[2].str());
        std::string rest = raw_line.substr(m.position(0) + m.length(0));

        LyricLine line;
        line.start_time = line_time;

        // Enhanced LRC: "<mm:ss.xx>word <mm:ss.xx>word ..." — split on
        // word timestamps. Detected per line so a mixed file still parses.
        if (rest.find('<') != std::string::npos) {
            auto begin = std::sregex_iterator(rest.begin(), rest.end(), word_ts_re);
            auto end = std::sregex_iterator();
            std::vector<std::pair<double, size_t>> marks; // (time, text-start-offset)
            for (auto it = begin; it != end; ++it) {
                std::smatch wm = *it;
                double t = std::stod(wm[1].str()) * 60.0 + std::stod(wm[2].str());
                marks.emplace_back(t, static_cast<size_t>(wm.position(0) + wm.length(0)));
            }
            for (size_t i = 0; i < marks.size(); ++i) {
                size_t start = marks[i].second;
                size_t end_off = (i + 1 < marks.size())
                    ? rest.find('<', start)
                    : rest.size();
                if (end_off == std::string::npos) end_off = rest.size();
                std::string word = rest.substr(start, end_off - start);
                // trim
                while (!word.empty() && std::isspace((unsigned char)word.front())) word.erase(word.begin());
                while (!word.empty() && std::isspace((unsigned char)word.back())) word.pop_back();
                if (!word.empty()) {
                    line.words.emplace_back(marks[i].first, word);
                    if (!line.full_text.empty()) line.full_text += ' ';
                    line.full_text += word;
                }
            }
        }

        if (line.words.empty()) {
            // plain line-synced LRC (or enhanced parse yielded nothing usable)
            while (!rest.empty() && std::isspace((unsigned char)rest.front())) rest.erase(rest.begin());
            line.full_text = rest;
        }

        lines.push_back(std::move(line));
    }

    return lines;
}

LyricsResult fetch_lyrics(const std::string& title, const std::string& artist,
                          const ServerLyricsProvider& server_provider,
                          const fs::path& track_path, bool force_network) {
    LyricsResult result;
    (void)artist;

    // 1) Local sidecar file — no network call at all if this hits.
    std::string local_lrc;
    if (!force_network && load_sidecar(track_path, local_lrc)) {
        result.lines = parse_lrc(local_lrc);
        if (!result.lines.empty()) {
            result.status = LyricsStatus::Ok;
            result.source = "local";
            result.raw_lrc = local_lrc;
            result.message = "lyrics loaded (cached)";
            return result;
        }
        // fall through to the server chain if the sidecar was empty/unparseable
    }

    // 2) Server provider (Jellyfin /Audio/{id}/Lyrics). No lyrics on the
    //    server is a normal miss, not an error — report NotFound so the
    //    panel shows a calm "no lyrics" caption.
    if (server_provider) {
        std::string err;
        auto lrc = server_provider(&err);
        if (!lrc || lrc->empty()) {
            result.status = LyricsStatus::NotFound;
            result.message = err.empty() ? "no lyrics found for \"" + title + "\"" : err;
            return result;
        }

        result.lines = parse_lrc(*lrc);
        result.status = result.lines.empty() ? LyricsStatus::NotFound : LyricsStatus::Ok;
        result.source = "jellyfin";
        result.raw_lrc = *lrc;
        if (!result.lines.empty()) {
            bool word_synced = false;
            for (const auto& l : result.lines) if (!l.words.empty()) { word_synced = true; break; }
            result.message = (word_synced ? "word-synced lyrics" : "line-synced lyrics") + std::string(" (jellyfin)");
            save_sidecar(track_path, *lrc); // cache to disk for offline reuse next time
        } else {
            result.message = err.empty() ? "server lyrics contained no parseable lines" : err;
        }
        return result;
    }

    // 3) No provider (local-only track, app built without Jellyfin) and no
    //    sidecar — nothing else to try.
    result.status = LyricsStatus::NotFound;
    result.message = "no lyrics for \"" + title + "\"";
    return result;
}

} // namespace muisc