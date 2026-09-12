#pragma once
#include <string>

namespace muisc {

// Percent-encodes a string so it can be dropped into a URL query string
// (used for search terms, item ids, etc.).
std::string url_encode(const std::string& s);

// GET `url` with curl, expecting a JSON body. `auth_header` (a full
// "Name: value" header, e.g. the Jellyfin Authorization header) is passed
// through -H. On HTTP success fills `out` with the response body and
// returns true; otherwise fills `err` and returns false. `insecure`
// appends --insecure (self-signed LAN certificates).
bool http_get_json(const std::string& url, const std::string& auth_header,
                   bool insecure, std::string& out, std::string& err);

// Downloads `url` to `dest` with curl -o (binary-safe, nothing captured on
// stdout). Returns true only if curl exited 0 AND `dest` exists with a
// nonzero size.
bool http_download_to_file(const std::string& url, const std::string& auth_header,
                           bool insecure, const std::string& dest, std::string& err);

} // namespace muisc