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

// Scope for an online search (Task 5). The scope is mapped server-side to
// the `includeItemTypes` value in the Search/Hints request, so the results
// are filtered by the server, not client-side: All = Audio,MusicAlbum,
// Playlist plus the separate MusicArtist pass (the classic combined
// search); the typed scopes (Playlist/Artist/Album) each include ONLY
// that one type.
enum class OnlineSearchScope {
    All,       // default / s:  — tracks + albums + playlists + artists
    Playlist,  // p:            — playlists only (empty p: == list all)
    Artist,    // a:            — artists only (no songs, no albums)
    Album,     // b:            — albums only
};
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
    // plus a second query with Artist so music artists show up too (the
    // server suppresses artist hits when other types are combined, so they
    // have to come from a separate request). Only "MusicArtist" hints are
    // kept (raw "Person" people are dropped). type = one of "Audio",
    // "Album", "Playlist", "MusicArtist".
    //
    // With a non-All `scope` the search is narrowed server-side to exactly
    // that one type (single request, no Artist second pass): Playlist ->
    // Playlist hints, Artist -> MusicArtist only (raw Person dropped),
    // Album -> MusicAlbum hints. An empty query with scope Playlist falls
    // back to list_playlists() (the same request the add-to-playlist
    // picker uses), so a bare "/p:" lists every playlist.
    std::vector<OnlineResult> search(const std::string& query, int count = 50,
                                     std::string* error_out = nullptr,
                                     OnlineSearchScope scope = OnlineSearchScope::All);

    // GET /Items?ParentId=<id>&UserId=<user>&IncludeItemTypes=Audio: the
    // track list inside an album or playlist container. Also used to poke
    // the first user id into user_id_ so playlist queries work (albums
    // don't strictly need it). Returns the contained Audio items.
    std::vector<OnlineResult> list_children(const std::string& parent_id,
                                            std::string* error_out = nullptr);

    // GET /Items?ArtistIds=<id>&IncludeItemTypes=Audio&Recursive=true:
    // every track by a music artist. Used when the user drills into or
    // queue-adds an artist result.
    std::vector<OnlineResult> list_artist_tracks(const std::string& artist_id,
                                                 std::string* error_out = nullptr);

    // GET /Items?IncludeItemTypes=Audio&Recursive=true&SortBy=DateCreated
    // &SortOrder=Descending&StartIndex=<start>&Limit=<limit>: the whole
    // music library, newest-added first. `limit`/`start_index` paginate
    // (the app loads the list lazily as the user scrolls). `total_out`
    // receives the server's TotalRecordCount so callers know whether more
    // pages exist.
    std::vector<OnlineResult> list_recent(int limit, int start_index,
                                          int* total_out = nullptr,
                                          std::string* error_out = nullptr);

    // GET /Items/{id} for the container/metadata, then downloads
    // GET /Audio/{id}/stream into the cache (downloaded-once, like the
    // old yt-dlp cache) so the existing ffprobe → ffmpeg-decode → Player
    // pipeline runs against a local file with seek/waveform/'y'-save all
    // intact.
    std::optional<SongResult> resolve_by_id(const std::string& item_id, const std::string& title,
                                            const std::string& artist, std::string* error_out = nullptr);

    // GET /Items?IncludeItemTypes=Playlist&Recursive=true (user-scoped):
    // the user's Jellyfin playlists, as container rows. Used both to
    // display an "add to playlist" target list and (indirectly, through
    // resolve_container_children) to expand one into its tracks.
    std::vector<OnlineResult> list_playlists(std::string* error_out = nullptr);

    // POST /Playlists/{playlist_id}/Items?Ids={item_id}&UserId=... -- adds
    // a single item to a playlist. Jellyfin auto-expands container ids
    // (Album/MusicArtist/Playlist → their Audio children) server-side, so
    // passing one album/artist/playlist id adds the whole container. The
    // server responds 204 No Content when the item was already a member or
    // the container expanded successfully.
    bool add_to_playlist(const std::string& playlist_id, const std::string& item_id,
                         std::string* error_out = nullptr);

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