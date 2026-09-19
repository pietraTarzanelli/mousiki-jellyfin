#include "http_util.h"
#include "process_util.h"
#include <cctype>
#include <filesystem>
#include <system_error>

namespace muisc {

namespace fs = std::filesystem;

std::string url_encode(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

namespace {

// Shared curl invocation. Text fetches (JSON) capture stdout; file
// downloads write through -o. stdin is already /dev/null'd inside
// run_capture, which is what keeps curl from ever touching our raw-mode
// terminal (same reason this matters for ffmpeg).
std::string curl_base(const std::string& url, const std::string& auth_header, bool insecure) {
    std::string cmd = "curl -sS --fail-with-body --max-time 600 --retry 2 --retry-delay 1";
    if (insecure) cmd += " --insecure";
    cmd += " -H " + shell_quote(auth_header);
    cmd += " -H 'Accept: application/json'";
    cmd += " " + shell_quote(url);
    return cmd;
}

} // namespace

bool http_get_json(const std::string& url, const std::string& auth_header,
                   bool insecure, std::string& out, std::string& err) {
    ProcResult r = run_capture(curl_base(url, auth_header, insecure), /*merge_stderr=*/true);
    out = r.out;
    if (r.ok()) return true;
    err = "HTTP request failed (curl exit " + std::to_string(r.exit_code) + ")";
    // --fail-with-body keeps the body (often a Jellyfin error JSON) on
    // stdout even on HTTP error status — surface a snippet of it.
    if (!r.out.empty()) {
        std::string snippet = r.out.substr(0, std::min<size_t>(r.out.size(), 200));
        err += ": " + snippet;
    }
    return false;
}

bool http_download_to_file(const std::string& url, const std::string& auth_header,
                           bool insecure, const std::string& dest, std::string& err) {
    std::string cmd = curl_base(url, auth_header, insecure);
    cmd += " -o " + shell_quote(dest);
    ProcResult r = run_capture(cmd, /*merge_stderr=*/true);
    if (!r.ok()) {
        err = "download failed (curl exit " + std::to_string(r.exit_code) + ")";
        if (!r.out.empty()) {
            std::string snippet = r.out.substr(0, std::min<size_t>(r.out.size(), 200));
            err += ": " + snippet;
        }
        return false;
    }
    // curl can still report success without writing anything useful
    // (e.g. a 200 with an empty body) — treat that as failure too.
    std::error_code ec;
    if (!fs::exists(dest, ec) || fs::file_size(dest, ec) <= 0) {
        err = "download produced no data";
        return false;
    }
    return true;
}

bool http_raw(const std::string& url, const std::string& auth_header, bool insecure,
              const std::string& method, const std::string& data,
              std::string& out, std::string& err) {
    std::string cmd = "curl -sS --fail-with-body --max-time 120 --retry 1 --retry-delay 1";
    if (insecure) cmd += " --insecure";
    cmd += " -X " + shell_quote(method);
    cmd += " -H " + shell_quote(auth_header);
    cmd += " -H 'Accept: application/json'";
    if (!data.empty()) cmd += " -d " + shell_quote(data);
    cmd += " " + shell_quote(url);
    ProcResult r = run_capture(cmd, /*merge_stderr=*/true);
    out = r.out;
    if (r.ok()) return true;
    err = "HTTP " + method + " failed (curl exit " + std::to_string(r.exit_code) + ")";
    if (!r.out.empty()) {
        err += ": " + r.out.substr(0, std::min<size_t>(r.out.size(), 200));
    }
    return false;
}

} // namespace muisc