#include "VectorConsole.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace VectorConsole {
namespace {

#ifdef _WIN32
HANDLE g_hOut = INVALID_HANDLE_VALUE;
HANDLE g_hIn = INVALID_HANDLE_VALUE;
bool   g_isConsole = false;
bool   g_tuiActive = false;

DWORD  g_savedOutMode = 0;
UINT   g_savedOutCp = 0;
DWORD  g_savedInMode = 0;
bool   g_savedOutModeOk = false;
bool   g_savedInModeOk = false;
bool   g_cpSaved = false;

std::thread g_inputThread;
std::atomic<bool> g_inputStop{ false };
std::atomic<bool> g_relayouting{ false };

// stdout/stderr capture (see StartStdoutCapture below)
int  g_savedFdOut = -1;
int  g_savedFdErr = -1;
int  g_pipeRead = -1;
int  g_pipeWrite = -1;
std::thread g_readerThread;
std::atomic<bool> g_readerStop{ false };
#endif

int  g_width = 100;
int  g_height = 30;
int  g_viewLeft = 0;    // visible viewport origin inside the screen buffer
int  g_viewTop = 0;
int  g_uiRows = 14;
int  g_logScroll = 0;            // 0 == newest line visible at the bottom
long long g_framesDrawn = 0;
long long g_logsAppended = 0;
long long g_writeFailures = 0;   // non-zero means drawing is silently failing

std::mutex g_mutex;        // guards g_logHistory / g_logScroll
std::mutex g_writeMutex;   // serialises header and log-pane console writes
std::deque<std::wstring> g_logHistory;

// ------------------------------------------------------------------ encoding
#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], need);
    return out;
}
#endif

// Removes ANSI escape sequences (used for log lines, which we re-colour
// ourselves or print plainly).
std::string StripAnsi(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if ((unsigned char)s[i] == 0x1B && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size()) {
                char c = s[i++];
                if (c >= '@' && c <= '~') break;
            }
        } else {
            out.push_back(s[i++]);
        }
    }
    return out;
}

#ifdef _WIN32
// ------------------------------------------------------------------ grid model
struct Cell {
    wchar_t ch = L' ';
    WORD    attr = 7;
};

struct Grid {
    int rows = 0, cols = 0;
    std::vector<Cell> cells;
    Grid(int r, int c) : rows(r), cols(c), cells((size_t)r * c) {}
    Cell& at(int r, int c) { return cells[(size_t)r * cols + c]; }
};

WORD AttrForSgr(const std::vector<int>& params, WORD current) {
    WORD a = current;
    for (int p : params) {
        switch (p) {
        case 0:  a = 7;  break;                 // reset -> default white
        case 1:  a |= FOREGROUND_INTENSITY; break;
        case 22: a &= (WORD)~FOREGROUND_INTENSITY; break;
        case 30: a = (a & 0xF0) | 0; break;
        case 31: a = (a & 0xF0) | FOREGROUND_RED; break;
        case 32: a = (a & 0xF0) | FOREGROUND_GREEN; break;
        case 33: a = (a & 0xF0) | (FOREGROUND_RED | FOREGROUND_GREEN); break;
        case 34: a = (a & 0xF0) | FOREGROUND_BLUE; break;
        case 35: a = (a & 0xF0) | (FOREGROUND_RED | FOREGROUND_BLUE); break;
        case 36: a = (a & 0xF0) | (FOREGROUND_GREEN | FOREGROUND_BLUE); break;
        case 37: a = (a & 0xF0) | (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); break;
        default:
            if (p >= 90 && p <= 97) {
                WORD base = (WORD)(7 & 0);
                switch (p) {
                case 90: base = 0; break;
                case 91: base = FOREGROUND_RED; break;
                case 92: base = FOREGROUND_GREEN; break;
                case 93: base = FOREGROUND_RED | FOREGROUND_GREEN; break;
                case 94: base = FOREGROUND_BLUE; break;
                case 95: base = FOREGROUND_RED | FOREGROUND_BLUE; break;
                case 96: base = FOREGROUND_GREEN | FOREGROUND_BLUE; break;
                case 97: base = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
                }
                a = (WORD)((a & 0xF0) | base | FOREGROUND_INTENSITY);
            }
            break;
        }
    }
    return a;
}

// Parses the escape-coded frame that VectorUI produces into a cell grid.
// Handles CSI 2J (clear), CSI H / CSI r;cH (cursor), CSI ...m (attributes).
Grid ParseFrame(const std::string& frame) {
    Grid g(g_uiRows, g_width);
    WORD attr = 7;
    int row = 0, col = 0;

    size_t i = 0;
    while (i < frame.size()) {
        unsigned char c = (unsigned char)frame[i];
        if (c == 0x1B && i + 1 < frame.size() && frame[i + 1] == '[') {
            i += 2;
            std::string params;
            while (i < frame.size()) {
                char d = frame[i];
                if (d >= '@' && d <= '~') { ++i; break; }
                params.push_back(d);
                ++i;
            }
            char final = (i > 0) ? frame[i - 1] : 'm';
            if (final == 'm') {
                std::vector<int> nums;
                size_t start = 0;
                if (params.empty()) nums.push_back(0);
                while (start <= params.size()) {
                    size_t semi = params.find(';', start);
                    std::string tok = params.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
                    nums.push_back(tok.empty() ? 0 : atoi(tok.c_str()));
                    if (semi == std::string::npos) break;
                    start = semi + 1;
                }
                attr = AttrForSgr(nums, attr);
            } else if (final == 'H' || final == 'f') {
                int r = 1, cc = 1;
                size_t semi = params.find(';');
                if (semi != std::string::npos) {
                    r = atoi(params.substr(0, semi).c_str());
                    cc = atoi(params.substr(semi + 1).c_str());
                }
                row = (r > 0 ? r - 1 : 0);
                col = (cc > 0 ? cc - 1 : 0);
            } else if (final == 'J') {
                if (!params.empty() && params[0] == '2') {
                    for (auto& cell : g.cells) { cell.ch = L' '; cell.attr = 7; }
                }
            }
            continue;
        }
        if (c == '\n') { ++row; col = 0; ++i; continue; }
        if (c == '\r') { col = 0; ++i; continue; }

        // decode one UTF-8 code point
        wchar_t wch = L'?';
        int extra = 0;
        unsigned int cp = 0;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; extra = 3; }
        else { cp = c; extra = 0; }
        ++i;
        for (int k = 0; k < extra && i < frame.size(); ++k, ++i)
            cp = (cp << 6) | ((unsigned char)frame[i] & 0x3Fu);
        wch = (wchar_t)cp;

        if (row >= 0 && row < g.rows && col >= 0 && col < g.cols) {
            g.at(row, col).ch = wch;
            g.at(row, col).attr = attr;
        }
        ++col;
    }
    return g;
}

void WriteRowRuns(int row, const std::vector<Cell>& line) {
    // Never touch the last column: writing into the final cell of the bottom
    // row makes conhost scroll the whole buffer by one line, which smears the
    // header down into the log pane.
    const int limit = (g_width > 0) ? g_width : 1;
    int n = (int)line.size();
    if (n > limit) n = limit;

    COORD pos = { (SHORT)g_viewLeft, (SHORT)(g_viewTop + row) };
    if (!SetConsoleCursorPosition(g_hOut, pos)) ++g_writeFailures;
    int i = 0;
    while (i < n) {
        WORD a = line[(size_t)i].attr;
        int j = i;
        std::wstring run;
        while (j < n && line[(size_t)j].attr == a) { run.push_back(line[(size_t)j].ch); ++j; }
        SetConsoleTextAttribute(g_hOut, a);
        DWORD written = 0;
        if (!WriteConsoleW(g_hOut, run.c_str(), (DWORD)run.size(), &written, nullptr))
            ++g_writeFailures;
        i = j;
    }
    // Pad the remainder so stale characters from earlier frames are erased.
    if (n < limit) {
        std::wstring pad((size_t)(limit - n), L' ');
        SetConsoleTextAttribute(g_hOut, 7);
        DWORD written = 0;
        if (!WriteConsoleW(g_hOut, pad.c_str(), (DWORD)pad.size(), &written, nullptr))
            ++g_writeFailures;
    }
    SetConsoleTextAttribute(g_hOut, 7);
}

// ------------------------------------------------------------------ geometry
// Returns true when the console viewport had to be pinned back to the origin.
bool QueryGeometry() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(g_hOut, &csbi)) return false;

    // If SetConsoleScreenBufferSize could not shrink the buffer down to the
    // window, the console keeps native scrollback and the user can move the
    // viewport with the wheel or the scrollbar. Our drawing is viewport-relative,
    // so a moved viewport leaves the previous frame sitting in the buffer and
    // scrolling reveals a mess of leftover fragments. Pin the viewport back to
    // the origin every frame; the continuous repaint then erases the leftovers.
    bool pinned = false;
    if (csbi.srWindow.Top != 0 || csbi.srWindow.Left != 0) {
        SHORT w = (SHORT)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
        SHORT h = (SHORT)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
        SMALL_RECT win = { 0, 0, (SHORT)(w - 1), (SHORT)(h - 1) };
        if (SetConsoleWindowInfo(g_hOut, TRUE, &win)) {
            if (!GetConsoleScreenBufferInfo(g_hOut, &csbi)) {
                csbi.srWindow.Left = 0; csbi.srWindow.Top = 0;
                csbi.srWindow.Right = (SHORT)(w - 1);
                csbi.srWindow.Bottom = (SHORT)(h - 1);
            }
            pinned = true;
        }
    }

    g_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    g_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    g_viewLeft = csbi.srWindow.Left;
    g_viewTop = csbi.srWindow.Top;
    if (g_uiRows > g_height - 2) g_uiRows = (g_height > 3) ? g_height - 2 : 1;
    if (g_uiRows < 1) g_uiRows = 1;
    return pinned;
}

void ClampScroll() {
    int hist = (int)g_logHistory.size();
    int maxOff = hist > LogRows() ? hist - LogRows() : 0;
    if (g_logScroll > maxOff) g_logScroll = maxOff;
    if (g_logScroll < 0) g_logScroll = 0;
}

void RedrawLogPane() {
    QueryGeometry();
    int top = g_uiRows;
    int rows = g_height - top;
    if (rows <= 0) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    std::lock_guard<std::mutex> wlock(g_writeMutex);
    ClampScroll();

    int hist = (int)g_logHistory.size();
    int end = hist - g_logScroll;          // exclusive
    int begin = end - rows;
    if (begin < 0) begin = 0;

    for (int r = 0; r < rows; ++r) {
        int idx = begin + r;
        std::wstring text;
        if (idx >= 0 && idx < end && idx < hist) text = g_logHistory[idx];
        const int limit = (g_width > 0) ? g_width : 1;
        if ((int)text.size() > limit) text = text.substr(0, (size_t)limit);
        text.resize((size_t)limit, L' ');
        COORD pos = { (SHORT)g_viewLeft, (SHORT)(g_viewTop + top + r) };
        SetConsoleCursorPosition(g_hOut, pos);
        SetConsoleTextAttribute(g_hOut, 7);
        DWORD written = 0;
        WriteConsoleW(g_hOut, text.c_str(), (DWORD)text.size(), &written, nullptr);
    }
    SetConsoleTextAttribute(g_hOut, 7);
    g_relayouting = false;
}

void FullRedraw() {
    if (!g_isConsole) return;
    QueryGeometry();
    RedrawLogPane();
}

// ------------------------------------------------------------------ input
bool g_mouseCapture = false;

// Mouse-wheel scrolling and the console's native QuickEdit selection are
// mutually exclusive: ENABLE_MOUSE_INPUT hands the mouse to this process, so the
// console host stops doing drag-select + right-click copy. Keeping QuickEdit
// (the default) is what makes "select text and copy" work; --mouse-scroll opts
// into wheel scrolling instead, and F7 toggles between the two at runtime.
// Writes a raw VT sequence (used for the alternate-screen switch).
void WriteConsoleSeq(const wchar_t* seq) {
    if (g_hOut == INVALID_HANDLE_VALUE || seq == nullptr) return;
    size_t n = 0;
    while (seq[n]) ++n;
    DWORD written = 0;
    WriteConsoleW(g_hOut, seq, (DWORD)n, &written, nullptr);
}

void ApplyInputMode(bool mouseCapture) {
    if (!g_savedInModeOk || g_hIn == INVALID_HANDLE_VALUE) return;
    DWORD m = g_savedInMode | ENABLE_WINDOW_INPUT;
    if (mouseCapture) {
        m &= ~ENABLE_QUICK_EDIT_MODE;
        m |= ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT;
    } else {
        m &= ~ENABLE_MOUSE_INPUT;
    }
    SetConsoleMode(g_hIn, m);
    g_mouseCapture = mouseCapture;
}

void InputLoop() {
    HANDLE hIn = g_hIn;
    while (!g_inputStop.load()) {
        INPUT_RECORD rec;
        DWORD n = 0;
        if (!ReadConsoleInputW(hIn, &rec, 1, &n) || n == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }
        if (rec.EventType == MOUSE_EVENT &&
            (rec.Event.MouseEvent.dwEventFlags & MOUSE_WHEELED)) {
            if (g_mouseCapture) {
                short delta = (short)HIWORD(rec.Event.MouseEvent.dwButtonState);
                ScrollLog(delta > 0 ? 3 : -3);
            }
        } else if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            // Scrollback navigation so history is reachable without a mouse.
            int page = LogRows() > 1 ? LogRows() - 1 : 1;
            switch (rec.Event.KeyEvent.wVirtualKeyCode) {
            case VK_PRIOR: ScrollLog(page);  break;   // PageUp
            case VK_NEXT:  ScrollLog(-page); break;   // PageDown
            case VK_UP:    ScrollLog(1);     break;
            case VK_DOWN:  ScrollLog(-1);    break;
            case VK_HOME:  ScrollLog(1 << 20);  break;  // oldest
            case VK_END:   ScrollLog(-(1 << 20)); break; // newest
            case VK_F7:                                 // toggle mouse capture
                ApplyInputMode(!g_mouseCapture);
                AppendLog(g_mouseCapture
                    ? "[UI] mouse wheel scrolling ON  - hold Shift to select/copy text"
                    : "[UI] mouse wheel scrolling OFF - drag to select, right-click to copy");
                break;
            default: break;
            }
        } else if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            g_relayouting = true;
            FullRedraw();
        }
    }
}

// ---------------------------------------------------------------------------
// stdout/stderr capture
//
// Redirecting std::cout's streambuf alone is not enough: printf, puts and the
// embedded Python interpreter's print() write straight to the CRT's fd 1/2 and
// would land on the console at the current cursor position. A newline there
// scrolls the whole buffer and pushes the header off-screen -- which is exactly
// the "text appears and everything gets shoved out" behaviour.
//
// So fd 1 and fd 2 are pointed at a pipe and a reader thread turns whatever
// arrives into log-pane lines. After this, the only writer left on the console
// is our own drawing code.
void ReaderLoop() {
    char buf[4096];
    std::string pending;
    while (!g_readerStop.load()) {
        int got = _read(g_pipeRead, buf, (unsigned)sizeof(buf));
        if (got <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }
        pending.append(buf, (size_t)got);
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pending.erase(0, pos + 1);
            AppendLog(line);
        }
    }
}

void StartStdoutCapture() {
    if (g_pipeRead >= 0) return;
    int fds[2] = { -1, -1 };
    if (_pipe(fds, 65536, _O_BINARY) != 0) return;
    g_pipeRead = fds[0];
    g_pipeWrite = fds[1];
    g_savedFdOut = _dup(1);
    g_savedFdErr = _dup(2);
    _dup2(g_pipeWrite, 1);
    _dup2(g_pipeWrite, 2);

    // Once stdout is a pipe instead of a console, both the CRT and the embedded
    // Python interpreter switch to *block* buffering, which holds plugin output
    // back (or loses it entirely) and, for Python, also switches the text
    // encoding to the ANSI code page. Force immediate, UTF-8 output so plugin
    // lines reach the log pane line by line and Chinese does not turn to mush.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    _putenv_s("PYTHONUNBUFFERED", "1");
    _putenv_s("PYTHONIOENCODING", "utf-8");

    g_readerStop = false;
    g_readerThread = std::thread(ReaderLoop);
}

void StopStdoutCapture() {
    if (g_pipeRead < 0) return;
    g_readerStop = true;
    if (g_pipeWrite >= 0) {
        // Wake the blocking _read so the thread can observe the stop flag.
        const char nl = '\n';
        _write(g_pipeWrite, &nl, 1);
    }
    if (g_readerThread.joinable()) g_readerThread.join();
    if (g_savedFdOut >= 0) { _dup2(g_savedFdOut, 1); _close(g_savedFdOut); g_savedFdOut = -1; }
    if (g_savedFdErr >= 0) { _dup2(g_savedFdErr, 2); _close(g_savedFdErr); g_savedFdErr = -1; }
    if (g_pipeRead >= 0) { _close(g_pipeRead); g_pipeRead = -1; }
    if (g_pipeWrite >= 0) { _close(g_pipeWrite); g_pipeWrite = -1; }
}
#endif // _WIN32

} // namespace

// ==========================================================================
// Output setup (always on)
// ==========================================================================
bool SetupOutput() {
#ifdef _WIN32
    // Console window / tab title. Done first so it applies even when stdout is
    // not a console (the title is independent of the output handle).
    SetConsoleTitleW(L"Vector-lurete");

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
        g_savedOutMode = mode;
        g_savedOutModeOk = true;
        // Interpret ANSI colours (Logger emits them) instead of printing "[0m".
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        // The application is compiled with /utf-8, so its literals are UTF-8
        // bytes; the console must decode them as UTF-8.
        g_savedOutCp = GetConsoleOutputCP();
        g_cpSaved = (g_savedOutCp != 0);
        SetConsoleOutputCP(CP_UTF8);
        return true;
    }
#endif
    return false;
}

void RestoreOutput() {
#ifdef _WIN32
    if (g_cpSaved) SetConsoleOutputCP(g_savedOutCp);
    if (g_savedOutModeOk && g_hOut != INVALID_HANDLE_VALUE)
        SetConsoleMode(g_hOut, g_savedOutMode);
#endif
}

// ==========================================================================
// Two-region TUI
// ==========================================================================
bool IsConsole() {
#ifdef _WIN32
    return g_isConsole;
#else
    return false;
#endif
}

bool Active() {
#ifdef _WIN32
    return g_tuiActive;
#else
    return false;
#endif
}

bool Init(int uiRows, bool mouseScroll) {
#ifdef _WIN32
    // Open our OWN console handles. StartStdoutCapture() below calls _dup2() to
    // point fd 1/2 at a pipe, and the CRT closes the handle that fd 1 wrapped --
    // which is exactly the handle GetStdHandle(STD_OUTPUT_HANDLE) returns. Any
    // drawing through that handle would then fail silently. CONOUT$/CONIN$ give
    // us independent handles that _dup2() cannot invalidate.
    g_hOut = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
    if (g_hOut == INVALID_HANDLE_VALUE) g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hIn = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, 0, nullptr);
    if (g_hIn == INVALID_HANDLE_VALUE) g_hIn = GetStdHandle(STD_INPUT_HANDLE);

    DWORD mode = 0;
    if (g_hOut == INVALID_HANDLE_VALUE || !GetConsoleMode(g_hOut, &mode)) {
        g_isConsole = false;   // redirected: fall back to raw byte output
        g_uiRows = uiRows > 0 ? uiRows : 14;
        return false;
    }
    g_isConsole = true;
    g_uiRows = uiRows > 0 ? uiRows : 14;
    SetupOutput();

    // Switch to the alternate screen buffer -- what full-screen TUI apps do.
    // Windows Terminal keeps its OWN scrollback, independent of the console
    // screen buffer: shrinking the buffer and pinning the viewport (both of
    // which we already do) does NOT stop the wheel from scrolling back through
    // every frame we have painted, which shows up as a stack of leftover header
    // fragments. The alternate screen has no scrollback at all, so scrolling
    // cannot reveal anything; log history lives in our own log pane instead.
    WriteConsoleSeq(L"\x1b[?1049h");

    // Make the buffer exactly the window height so the console never scrolls on
    // its own -- the header must stay pinned and only the log pane may move.
    // SetConsoleScreenBufferSize refuses to shrink below the window, so first
    // grow if needed, then move the window to the origin, then shrink.
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(g_hOut, &csbi)) {
        SHORT winW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        SHORT winH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

        SHORT wantW = (SHORT)(winW + 1);
        if (csbi.dwSize.X < wantW || csbi.dwSize.Y < winH) {
            COORD grow = { (SHORT)(csbi.dwSize.X < wantW ? wantW : csbi.dwSize.X),
                           (SHORT)(csbi.dwSize.Y < winH ? winH : csbi.dwSize.Y) };
            SetConsoleScreenBufferSize(g_hOut, grow);
        }

        SMALL_RECT win = { 0, 0, (SHORT)(winW - 1), (SHORT)(winH - 1) };
        SetConsoleWindowInfo(g_hOut, TRUE, &win);

        // One column wider than the window. Writing the last *visible* column
        // then leaves the cursor inside the buffer instead of wrapping to the
        // next line (which would scroll the whole screen). With that safety we
        // can paint every visible cell, so no stale characters survive in the
        // rightmost column. If the shrink still fails, QueryGeometry() pins the
        // viewport every frame so native scrollback cannot smear the UI.
        COORD buf = { wantW, winH };
        SetConsoleScreenBufferSize(g_hOut, buf);
    }
    QueryGeometry();

    // Hide the cursor and clear.
    CONSOLE_CURSOR_INFO ci;
    if (GetConsoleCursorInfo(g_hOut, &ci)) {
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(g_hOut, &ci);
    }
    // Clear row by row, one column short of the width, so the clear itself can
    // never write the bottom-right cell (which would scroll the buffer).
    DWORD written = 0;
    const DWORD clearW = (DWORD)((g_width > 0) ? g_width : 1);
    for (int r = 0; r < g_height; ++r) {
        COORD c = { (SHORT)g_viewLeft, (SHORT)(g_viewTop + r) };
        FillConsoleOutputCharacterW(g_hOut, L' ', clearW, c, &written);
        FillConsoleOutputAttribute(g_hOut, 7, clearW, c, &written);
    }

    // Input: keyboard scrollback always; mouse-wheel scrolling is opt-in because
    // capturing the mouse disables the console's own drag-select + copy.
    DWORD inMode = 0;
    if (g_hIn != INVALID_HANDLE_VALUE && GetConsoleMode(g_hIn, &inMode)) {
        g_savedInMode = inMode;
        g_savedInModeOk = true;
        ApplyInputMode(mouseScroll);
        g_inputStop = false;
        g_inputThread = std::thread(InputLoop);
    }

    // Take over fd 1/2 so printf / puts / Python print() land in the log pane
    // instead of writing to the console and scrolling the header away.
    StartStdoutCapture();

    g_tuiActive = true;
    return true;
#else
    (void)uiRows;
    (void)mouseScroll;
    g_uiRows = uiRows > 0 ? uiRows : 14;
    return false;
#endif
}

void Shutdown() {
#ifdef _WIN32
    StopStdoutCapture();
    if (g_inputThread.joinable()) {
        g_inputStop = true;
        // Nudge the blocking ReadConsoleInput by writing an input event.
        if (g_hIn != INVALID_HANDLE_VALUE) {
            INPUT_RECORD rec;
            ZeroMemory(&rec, sizeof(rec));
            rec.EventType = FOCUS_EVENT;
            rec.Event.FocusEvent.bSetFocus = TRUE;
            DWORD w = 0;
            WriteConsoleInputW(g_hIn, &rec, 1, &w);
        }
        g_inputThread.join();
    }
    if (g_hIn != INVALID_HANDLE_VALUE && g_savedInModeOk) SetConsoleMode(g_hIn, g_savedInMode);
    if (g_hOut != INVALID_HANDLE_VALUE) {
        CONSOLE_CURSOR_INFO ci;
        if (GetConsoleCursorInfo(g_hOut, &ci)) {
            ci.bVisible = TRUE;
            SetConsoleCursorInfo(g_hOut, &ci);
        }
        COORD origin = { 0, (SHORT)(g_height - 1) };
        SetConsoleCursorPosition(g_hOut, origin);
    }
    g_tuiActive = false;
    // Leave the alternate screen (must happen while VT processing is still on).
    WriteConsoleSeq(L"\x1b[?1049l");
    RestoreOutput();
#endif
}

void SetUiRows(int rows) {
    if (rows < 1) rows = 1;
#ifdef _WIN32
    if (g_isConsole && rows > g_height - 2) rows = (g_height > 3) ? g_height - 2 : 1;
#endif
    g_uiRows = rows;
#ifdef _WIN32
    if (g_tuiActive) FullRedraw();
#endif
}

int UiRows() { return g_uiRows; }

int LogRows() {
    int r = g_height - g_uiRows;
    return r > 0 ? r : 0;
}

void Present(const std::string& ansiFrame) {
#ifdef _WIN32
    if (!g_isConsole) {
        std::fwrite(ansiFrame.data(), 1, ansiFrame.size(), stdout);
        std::fflush(stdout);
        return;
    }
    bool pinned = QueryGeometry();
    Grid g = ParseFrame(ansiFrame);
    {
        std::lock_guard<std::mutex> wlock(g_writeMutex);
        ++g_framesDrawn;
        for (int r = 0; r < g.rows; ++r) {
            std::vector<Cell> line(g.cells.begin() + (size_t)r * g.cols,
                                   g.cells.begin() + (size_t)(r + 1) * g.cols);
            WriteRowRuns(r, line);
        }
    }
    // After re-pinning, rows below the header may still hold fragments from the
    // old viewport position, so repaint the pane too (outside the write lock).
    if (pinned) RedrawLogPane();
#else
    std::fwrite(ansiFrame.data(), 1, ansiFrame.size(), stdout);
    std::fflush(stdout);
#endif
}

// ------------------------------------------------------------------ log pane
void AppendLog(const std::string& utf8Line) {
#ifdef _WIN32
    if (!g_isConsole) {
        std::fwrite(utf8Line.data(), 1, utf8Line.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
        return;
    }
    std::wstring w = Utf8ToWide(StripAnsi(utf8Line));
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_logHistory.push_back(std::move(w));
        ++g_logsAppended;
        const size_t kMaxHistory = 5000;
        while (g_logHistory.size() > kMaxHistory) g_logHistory.pop_front();
        // If the user is scrolled back, keep their view stable.
        if (g_logScroll > 0) ++g_logScroll;
    }
    RedrawLogPane();
#else
    std::fwrite(utf8Line.data(), 1, utf8Line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
#endif
}

void ScrollLog(int deltaLines) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_logScroll += deltaLines;
    }
    RedrawLogPane();
}

int LogScrollOffset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_logScroll;
}

// ------------------------------------------------------------------- logger
// Instead of modifying Logger, redirect std::cout's streambuf: everything the
// application logs through std::cout then lands in the log pane rather than
// being written over the animated header.
namespace {
bool g_sinkInstalled = false;

class PaneStreambuf : public std::streambuf {
public:
    std::streambuf* saved = nullptr;

protected:
    int_type overflow(int_type ch) override {
        if (ch == traits_type::eof()) return traits_type::not_eof(ch);
        char c = (char)ch;
        if (c == '\n') {
            AppendLog(line_);
            line_.clear();
        } else if (c != '\r') {
            line_.push_back(c);
        }
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; ++i) overflow((unsigned char)s[i]);
        return n;
    }

private:
    std::string line_;
};

PaneStreambuf g_paneBuf;
} // namespace

void InstallLoggerSink() {
    if (g_sinkInstalled) return;
    g_paneBuf.saved = std::cout.rdbuf(&g_paneBuf);
    g_sinkInstalled = true;
}

void RemoveLoggerSink() {
    if (!g_sinkInstalled) return;
    if (g_paneBuf.saved) std::cout.rdbuf(g_paneBuf.saved);
    g_sinkInstalled = false;
}

bool LoggerSinkInstalled() { return g_sinkInstalled; }

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
void DumpDiagnostics(const std::string& path) {
#ifdef _WIN32
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "VectorConsole diagnostics\n");
    fprintf(f, "  isConsole    = %d\n", g_isConsole ? 1 : 0);
    fprintf(f, "  tuiActive    = %d\n", g_tuiActive ? 1 : 0);
    fprintf(f, "  width        = %d\n", g_width);
    fprintf(f, "  height       = %d\n", g_height);
    fprintf(f, "  viewport     = (%d,%d)\n", g_viewLeft, g_viewTop);
    fprintf(f, "  uiRows       = %d\n", g_uiRows);
    fprintf(f, "  logRows      = %d\n", LogRows());
    fprintf(f, "  logHistory   = %d\n", (int)g_logHistory.size());
    fprintf(f, "  logScroll    = %d\n", g_logScroll);
    fprintf(f, "  framesDrawn  = %lld\n", g_framesDrawn);
    fprintf(f, "  logsAppended = %lld\n", g_logsAppended);
    fprintf(f, "  writeFailures= %lld\n", g_writeFailures);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (g_hOut != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(g_hOut, &csbi)) {
        fprintf(f, "  bufferSize   = %dx%d\n", csbi.dwSize.X, csbi.dwSize.Y);
        fprintf(f, "  srWindow     = (%d,%d)-(%d,%d)\n",
                csbi.srWindow.Left, csbi.srWindow.Top, csbi.srWindow.Right, csbi.srWindow.Bottom);
        fprintf(f, "  cursor       = (%d,%d)\n", csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y);
        fprintf(f, "  attributes   = 0x%04X\n", csbi.wAttributes);
    }

    // Read the screen back so the rendered layout can be inspected offline.
    // The stdout handle is write-only, so open a read-capable CONOUT$ handle.
    if (g_isConsole) {
        HANDLE hRead = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
        if (hRead == INVALID_HANDLE_VALUE) hRead = g_hOut;
        int h = (g_height > 0 && g_height < 200) ? g_height : 40;
        int w = (g_width > 0 && g_width < 500) ? g_width : 120;
        fprintf(f, "  --- screen readback (%d rows x %d cols), handleOk=%d ---\n",
                h, w, (hRead != INVALID_HANDLE_VALUE) ? 1 : 0);
        for (int r = 0; r < h; ++r) {
            std::wstring line((size_t)w, L' ');
            DWORD got = 0;
            COORD c = { (SHORT)g_viewLeft, (SHORT)(g_viewTop + r) };
            if (!ReadConsoleOutputCharacterW(hRead, &line[0], (DWORD)w, c, &got)) {
                fprintf(f, "  (read failed at row %d, gle=%lu)\n", r, (unsigned long)GetLastError());
                break;
            }
            line.resize(got);
            char buf[2048];
            int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(),
                                        buf, (int)sizeof(buf) - 1, nullptr, nullptr);
            if (n < 0) n = 0;
            buf[n] = 0;
            fprintf(f, "  R%02d|%s|\n", r, buf);
        }
        if (hRead != INVALID_HANDLE_VALUE && hRead != g_hOut) CloseHandle(hRead);
    }
    fclose(f);
#else
    (void)path;
#endif
}

} // namespace VectorConsole
