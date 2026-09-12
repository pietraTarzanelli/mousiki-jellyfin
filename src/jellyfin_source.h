#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "cache_manager.h"

namespace muisc {

namespace fs = std::filesystem;

// A remote item as returned by Jellyfin search. `video_id` carries the
// Jellyfin item id — it's the same opaque string the rest of the app
// already shuffles around for the old YouTube flow, so queue/list/playback
// code needs no changes. `type` is "Audio" (a playable track), "Album" or
// "Playlist" (a container that expands into tracks on Enter).
struct OnlineResult {
    std::string video_id;
    std::string title;
    std::string uploader;
    std::string type; // "Audio" | "Album" | "Playlist" | ""
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

    // GET /Search/Hints?searchTerm=..&includeItemTypes=Audio,Album,Playlist
    // Returns hits of those three types mapped to OnlineResult (type -
    // "Audio" | "Album" | "Playlist"). An unreachable/misconfigured server
    // yields an error via error_out.
    std::vector<OnlineResult> search(const std::string& query, int count = 50,
                                     std::string* error_out = nullptr);

    // GET /Items?ParentId=<id>&UserId=<user>&IncludeItemTypes=Audio: the
    // track list inside an album or playlist container. Also used to poke
    // the first user id into user_id_ so playlist queries work (albums
    // don't strictly need it). Returns the contained Audio items.
    std::vector<OnlineResult> list_children(const std::string& parent_id,
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
    std::string user_id_; // first user Discovered lazily for playlist children

    std::string auth_header() const;

    // GET /Users and remember the first user id (playlist item lists are
    // user-scoped). Returns empty string if it can't be discovered.
    std::string resolve_user_id();
};

} // namespace muisc