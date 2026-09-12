#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace muisc {

namespace fs = std::filesystem;

// One LRC line, optionally with per-word timestamps (enhanced/A2 LRC),
// which is what makes word-level highlighting possible during playback.
struct LyricLine {
    double start_time = 0.0;
    std::string full_text;
    std::vector<std::pair<double, std::string>> words; // empty if not word-synced
};

enum class LyricsStatus {
    Ok,
    NotFound,   // no lyrics available for this track (server returned none, sidecar missing)
    Error,
};

struct LyricsResult {
    LyricsStatus status = LyricsStatus::Error;
    std::vector<LyricLine> lines;
    std::string message;   // human-readable status/error, shown in the lyrics panel
    std::string source;    // "local" | "jellyfin" | ""
    std::string raw_lrc;   // the raw LRC text, kept so it can be cached to a sidecar file
};

// Optional provider of server-side lyrics. Returns the LRC text, or
// nullopt if the server has no lyrics for this item (setting err_out to a
// short human-readable reason). Supplied by the app and wired to
// JellyfinSource::fetch_lyrics_lrc for Jellyfin tracks; a Jellyfin-only
// build passes no provider for local tracks, in which case only the
// sidecar chain applies.
using ServerLyricsProvider = std::function<std::optional<std::string>(std::string* err_out)>;

// Priority chain: a local sidecar .lrc file next to `track_path` (checked
// first, no network call at all) -> the server provider (Jellyfin's
// /Audio/{id}/Lyrics). Whatever comes back from the server is written back
// to the sidecar file, so the next time this track plays (even offline)
// it's a local-file hit.
LyricsResult fetch_lyrics(const std::string& title, const std::string& artist,
                          const ServerLyricsProvider& server_provider,
                          const fs::path& track_path = fs::path(),
                          bool force_network = false);

} // namespace muisc