#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "cache_manager.h"

namespace muisc {

namespace fs = std::filesystem;

// A remote track as returned by Jellyfin search. `video_id` carries the
// Jellyfin item id — it's the same opaque string the rest of the app
// already shuffles around for YouTube, so the queue/list/playback code
// needs no changes.
struct OnlineResult {
    std::string video_id;
    std::string title;
    std::string uploader;
};

struct SongResult {
    std::string title;
    std::string artist;   // best-effort, may be empty
    fs::path cached_path;
    bool from_cache = false;
};

// Talks to a Jellyfin server through its native REST API only (no Subsonic,
// no plugins). Authentication is a static API key sent as the
// `Authorization: MediaBrowser Token="<key>"` header on every request.
class JellyfinSource {
public:
    explicit JellyfinSource(CacheManager& cache) : cache_(cache) {}

    // Server URL + API key come from config.txt (JellyfinServerUrl /
    // JellyfinApiKey). `insecure` lets curl verify nothing (LAN self-
    // signed certs). Must be called once at startup; harmless no-op if
    // passed the same values.
    void configure(std::string base_url, std::string api_key, bool insecure);
    bool configured() const;

    // GET /Search/Hints?searchTerm=..&includeItemTypes=Audio&limit=..
    // Returns tracks whose type is Audio, mapped to OnlineResult. An
    // unreachable/misconfigured server yields an error via error_out.
    std::vector<OnlineResult> search(const std::string& query, int count = 30,
                                     std::string* error_out = nullptr);

    // GET /Items/{id} for the container/metadata, then downloads
    // GET /Audio/{id}/stream into the cache (downloaded-once, like the
    // old yt-dlp cache) so the existing ffprobe → ffmpeg-decode → Player
    // pipeline runs against a local file with seek/waveform/'y'-save all
    // intact.
    std::optional<SongResult> resolve_by_id(const std::string& item_id, const std::string& title,
                                            const std::string& artist, std::string* error_out = nullptr);

    // GET /Audio/{id}/Lyrics (Jellyfin 10.9+) and converts the LyricDto
    // (line timestamps + optional ELRC word cues, both in 100ns ticks)
    // into LRC text. Returns nullopt (with error_out set) if the server
    // has no lyrics for the item.
    std::optional<std::string> fetch_lyrics_lrc(const std::string& item_id, std::string* error_out = nullptr);

private:
    CacheManager& cache_;
    std::string base_url_;
    std::string api_key_;
    bool insecure_ = true;

    std::string auth_header() const;
};

} // namespace muisc