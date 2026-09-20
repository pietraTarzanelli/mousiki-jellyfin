#pragma once
#include <string>

namespace muisc {

// Mouse tracking is enabled as SGR extended mode (ESC[?1006h) plus plain
// button-press reporting (ESC[?1000h). Deliberately NOT enabled: motion
// (1002/1003) and drag tracking. Wheel scroll arrives from the terminal
// only as button codes, which poll_key() consumes and discards — that's
// what keeps the wheel from doing anything (Task 4: block scroll).
struct MouseEvent {
    int button = 0; // 0 = left press, 1 = middle, 2 = right, 3 = release; 64/65 = wheel up/down
    int x = 0;      // 1-based terminal column
    int y = 0;      // 1-based terminal row
};

// Raw, non-canonical, no-echo terminal mode + non-blocking key reads.
// Panel/box drawing lives in app.cpp; this is just the terminal plumbing.
class TerminalIO {
public:
    TerminalIO();
    ~TerminalIO();

    void restore();

    // Sentinel returned by poll_key() when a mouse event (not a key) was
    // read. Coordinates/button end up in last_mouse().
    static constexpr int kMouseEventCode = 0x10000;

    // Non-blocking single "logical" key read. Arrow keys (3-byte escape
    // sequences) collapse to 'A'/'B'/'C'/'D' (up/down/right/left). A lone
    // Escape key returns 27. Backspace returns 127. Returns 0 if nothing
    // is waiting. Returns kMouseEventCode for a parsed mouse event.
    int poll_key();

    // The most recent mouse event consumed via poll_key().
    const MouseEvent& last_mouse() const { return last_mouse_; }

    int rows() const;
    int cols() const;

private:
    bool raw_mode_active_ = false;
    MouseEvent last_mouse_;
    int parse_sgr_mouse(int first); // decodes "ESC [ < b;x;y M/m"
    int parse_x10_mouse();          // decodes "ESC [ M cb cx cy"
    void reassert_raw_mode(); // see poll_key()'s definition for why this exists
};

// Truncates/right-pads (by byte length — good enough for the mostly-ASCII
// UI text here; multi-byte titles may render slightly short) to exactly
// `width` visible columns.
std::string pad_right(const std::string& s, int width);
std::string pad_left(const std::string& s, int width);
std::string truncate_str(const std::string& s, int width);
std::string utf8_take(const std::string& s, int width);

// Computes terminal display width of a UTF-8 string based on wcwidth.
int display_width(const std::string& s);

} // namespace muisc
