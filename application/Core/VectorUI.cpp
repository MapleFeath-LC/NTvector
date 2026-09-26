#include "VectorUI.h"
#include "VectorConsole.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace VectorUI {
namespace {

// ---------------------------------------------------------------- constants
constexpr int  kBoxWidth = 100;
constexpr int  kMaxPos   = kBoxWidth - 12;

const char* kResetV  = "\033[0m";
const char* kBoldV   = "\033[1m";
const char* kGreenV  = "\033[92m";
const char* kBlueV   = "\033[94m";
const char* kWhiteV  = "\033[97m";
const char* kRedV    = "\033[91m";
const char* kYellowV = "\033[93m";
const char* kCyanV   = "\033[96m";
const char* kMagV    = "\033[95m";
const char* kHideCur = "\033[?25l";
const char* kShowCur = "\033[?25h";
const char* kClear   = "\033[2J";
const char* kHome    = "\033[H";

// ------------------------------------------------------------------- state
std::atomic<int>       g_tasks{ 0 };
std::atomic<long long> g_lastChatMs{ 0 };
std::atomic<int>       g_busyMin{ 1 };
std::atomic<int>       g_portalMin{ 3 };
std::atomic<long long> g_chatHoldMs{ 5000 };
std::atomic<bool>      g_running{ false };
std::atomic<bool>      g_stop{ false };
std::atomic<int>       g_fps{ 30 };
std::thread            g_thread;

// render-thread-only animation bookkeeping
int  g_pos = 0, g_dir = 1, g_wait = 0;
long long g_frame = 0;
int  g_portalPhase = 0, g_phaseFrame = 0;
State g_lastState = State::Idle;
bool  g_started = false;

#ifdef _WIN32
HANDLE g_hOut = INVALID_HANDLE_VALUE;
DWORD  g_savedMode = 0;
bool   g_modeSaved = false;
UINT   g_savedCp = 0;
bool   g_cpSaved = false;
#endif

long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void WriteOut(const std::string& s) {
    if (s.empty()) return;
    // Console path renders through WriteConsoleW into the fixed header region;
    // redirected output falls back to raw UTF-8/ANSI bytes.
    VectorConsole::Present(s);
}

// -------------------------------------------------------------- text helpers
// Decodes one UTF-8 code point starting at s[i], advancing i.
unsigned int DecodeUtf8(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { i += 1; return c; }
    unsigned int cp = 0;
    int extra = 0;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; extra = 3; }
    else { i += 1; return c; }
    i += 1;
    for (int k = 0; k < extra && i < s.size(); ++k, ++i) {
        unsigned char cc = (unsigned char)s[i];
        if ((cc & 0xC0) != 0x80) break;
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    return cp;
}

// Matches the original Python vis_width(): wide for code points > 0x2E80.
// NOTE: escape sequences are deliberately counted as width too, exactly like
// the Python version, so the rendered layout is unchanged.
int VisWidth(const std::string& s) {
    int w = 0;
    size_t i = 0;
    while (i < s.size()) {
        unsigned int cp = DecodeUtf8(s, i);
        w += (cp > 0x2E80u) ? 2 : 1;
    }
    return w;
}

std::string Spaces(int n) { return std::string(n > 0 ? n : 0, ' '); }

std::string CursorTo(int row, int col) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\033[%d;%dH", row, col);
    return std::string(buf);
}

// ---------------------------------------------------------------- the frame
std::vector<std::string> BigTitle() {
    static const char* nt[6] = {
        "███╗   ██╗████████╗",
        "████╗  ██║╚══██╔══╝",
        "██╔██╗ ██║   ██║   ",
        "██║╚██╗██║   ██║   ",
        "██║ ╚████║   ██║   ",
        "╚═╝  ╚═══╝   ╚═╝   ",
    };
    static const char* vec[6] = {
        "██╗   ██╗███████╗ ██████╗████████╗ ██████╗ ██████╗ ",
        "██║   ██║██╔════╝██╔════╝╚══██╔══╝██╔═══██╗██╔══██╗",
        "██║   ██║█████╗  ██║        ██║   ██║   ██║██████╔╝",
        "╚██╗ ██╔╝██╔══╝  ██║        ██║   ██║   ██║██╔══██╗",
        " ╚████╔╝ ███████╗╚██████╗   ██║   ╚██████╔╝██║  ██║",
        "  ╚═══╝  ╚══════╝ ╚═════╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝",
    };
    std::vector<std::string> out;
    for (int i = 0; i < 6; ++i) {
        out.push_back(std::string(kBoldV) + kGreenV + nt[i] + kResetV + "  " +
                      kBoldV + kBlueV + vec[i] + kResetV);
    }
    return out;
}

// Draws the frame: top/left/bottom edges only (no right edge), title centered.
std::vector<std::string> DrawBox(const std::vector<std::string>& titleLines,
                                 const std::string& statusText,
                                 const char* statusColor,
                                 bool statusRight) {
    std::vector<std::string> lines;
    const std::string empty = std::string(kWhiteV) + kBoldV + "┃" + kResetV +
                              Spaces(kBoxWidth + 2);
    // top edge
    std::string top = std::string(kWhiteV) + kBoldV + "┏";
    for (int i = 0; i < kBoxWidth + 2; ++i) top += "━";
    top += kResetV;
    lines.push_back(top);

    lines.push_back(empty);
    for (const auto& t : titleLines) {
        int vis = VisWidth(t);
        int total = (kBoxWidth + 2) - vis;
        if (total < 0) total = 0;
        lines.push_back(std::string(kWhiteV) + kBoldV + "┃" + kResetV +
                        Spaces(total / 2) + t);
    }
    lines.push_back(empty);

    // Status row: the state word on its side, plus a centred producer credit.
    // Built from visible widths and emitted left to right, so the credit sits
    // in the middle of the row regardless of where the state word is placed.
    {
        const int contentW = kBoxWidth + 2;
        const std::string credit = "Producer : Nell  -  lurete.cn";
        const int creditW = VisWidth(credit);
        int creditCol = (contentW - creditW) / 2;
        if (creditCol < 0) creditCol = 0;
        const int statusW = VisWidth(statusText);

        std::string row = std::string(kWhiteV) + kBoldV + "┃" + kResetV;

        if (!statusRight) {
            // state word on the left
            row += " ";
            row += statusColor; row += statusText; row += kResetV;
            int gap = creditCol - 1 - statusW;
            if (gap > 0) row += Spaces(gap);
            row += kWhiteV; row += credit; row += kResetV;
            int tail = contentW - creditCol - creditW;
            if (tail > 0) row += Spaces(tail);
        } else {
            // state word on the right
            if (creditCol > 0) row += Spaces(creditCol);
            row += kWhiteV; row += credit; row += kResetV;
            int pad = contentW - statusW - 1;
            if (pad < 0) pad = 0;
            int gap = pad - creditCol - creditW;
            if (gap > 0) row += Spaces(gap);
            row += statusColor; row += statusText; row += kResetV;
            row += " ";
        }
        lines.push_back(row);
    }

    lines.push_back(empty);
    std::string bot = std::string(kWhiteV) + kBoldV + "┗";
    for (int i = 0; i < kBoxWidth + 2; ++i) bot += "━";
    bot += kResetV;
    lines.push_back(bot);
    return lines;
}

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += "\n";
        s += v[i];
    }
    return s;
}

// Advances the shared left/right pacer. Returns the new position.
int Pace(int stepInterval) {
    ++g_wait;
    if (g_wait >= stepInterval) {
        g_wait = 0;
        g_pos += g_dir;
        if (g_pos >= kMaxPos) { g_pos = kMaxPos; g_dir = -1; }
        if (g_pos <= 0)       { g_pos = 0;       g_dir = 1;  }
    }
    return g_pos;
}

// --- state 1: idle (fast pacing, chirping) ---
void DrawIdle() {
    int pos = Pace(2);
    const char* eye = "(^ o ^)";
    if (g_frame % 20 == 5)      eye = "(^ - ^)";
    else if (g_frame % 20 == 6) eye = "(^ ^ ^)";

    auto box = DrawBox(BigTitle(), "空闲", kGreenV, /*statusRight=*/false);
    box.push_back("");
    box.push_back(Spaces(2 + pos) + kYellowV + eye + kResetV + "  " + kCyanV + "啾~" + kResetV);
    WriteOut(std::string(kClear) + kHome + Join(box));
}

// --- state 2: busy (slow pacing, blinking) ---
void DrawBusy() {
    int pos = Pace(12);
    int cycle = (int)(g_frame % 24);
    const char* eye = "( o   o )";
    if (cycle == 10 || cycle == 11) eye = "( -   - )";
    else if (cycle == 12)           eye = "( _   _ )";
    else if (cycle == 13)           eye = "( -   - )";

    auto box = DrawBox(BigTitle(), "忙碌", kRedV, /*statusRight=*/true);
    box.push_back("");
    box.push_back(Spaces(2 + pos) + kYellowV + eye + kResetV);
    WriteOut(std::string(kClear) + kHome + Join(box));
}

// --- state 3: chat invoked or >= 3 tasks -> portal sequence (loops) ---
constexpr int kTopY = 2, kBottomY = 11, kLeftX = 3;
constexpr int kRightX = kBoxWidth - 1;
constexpr int kPortalX = kBoxWidth / 2 + 2, kPortalY = 14;
constexpr int kExitPortalX = kPortalX - 25, kExitPortalY = 14;

std::string Ghost() { return std::string(kYellowV) + kBoldV + "( O   O )" + kResetV; }

std::string PortalColumn(int x, int y, int height) {
    std::string s;
    for (int k = 0; k < height; ++k)
        s += CursorTo(y + k, x) + kMagV + kBoldV + "┃" + kResetV;
    return s;
}

void DrawPortal() {
    auto box = DrawBox(BigTitle(), "崩溃", kYellowV, /*statusRight=*/false);
    std::string out = std::string(kClear) + kHome + Join(box);

    const int w = kRightX - kLeftX;
    const int h = kBottomY - kTopY;
    const int perimeter = 2 * (w + h);
    const std::string ghost = Ghost();

    // phase durations in frames (30 fps), mirroring the Python timings
    static const int kDurations[8] = { 200, 15, 11, 9, 9, 9, 8, 20 };

    switch (g_portalPhase) {
    case 0: { // walk the inner perimeter
        int cycle = (int)(g_frame % perimeter);
        int cy, cx;
        if (cycle < w)                 { cy = kTopY;                 cx = kLeftX + cycle; }
        else if (cycle < w + h)        { cy = kTopY + (cycle - w);   cx = kRightX; }
        else if (cycle < 2 * w + h)    { cy = kBottomY;              cx = kRightX - (cycle - w - h); }
        else                           { cy = kBottomY - (cycle - 2 * w - h); cx = kLeftX; }
        out += CursorTo(cy, cx) + ghost;
        break;
    }
    case 1: { // portal opens under the frame
        int step = g_phaseFrame / 5 + 1;
        out += PortalColumn(kPortalX, kPortalY, step > 3 ? 3 : step);
        break;
    }
    case 2: { // mascot walks from frame bottom to the portal
        int startX = (kLeftX + kRightX) / 2, startY = kBottomY;
        const int steps = 10;
        int i = g_phaseFrame > steps ? steps : g_phaseFrame;
        int cy = startY + (kPortalY - startY) * i / steps;
        int cx = startX + (kPortalX - startX) * i / steps;
        out += PortalColumn(kPortalX, kPortalY, 3);
        out += CursorTo(cy, cx) + ghost;
        break;
    }
    case 3: // fade out inside the portal
        out += PortalColumn(kPortalX, kPortalY, 3);
        break;
    case 4: { // portal closes
        int height = 3 - (g_phaseFrame / 3);
        if (height > 0) out += PortalColumn(kPortalX, kPortalY, height);
        break;
    }
    case 5: { // exit portal appears
        int step = g_phaseFrame / 3 + 1;
        out += PortalColumn(kExitPortalX, kExitPortalY, step > 3 ? 3 : step);
        break;
    }
    case 6: { // mascot emerges from the exit portal
        out += PortalColumn(kExitPortalX, kExitPortalY, 3);
        out += CursorTo(kExitPortalY + 1, kExitPortalX + 1 + g_phaseFrame) + ghost;
        break;
    }
    default: { // portal vanishes, mascot stands
        if (g_phaseFrame < 5) out += PortalColumn(kExitPortalX, kExitPortalY, 3);
        out += CursorTo(kExitPortalY + 1, kExitPortalX + 9) + ghost;
        break;
    }
    }

    WriteOut(out);

    if (++g_phaseFrame >= kDurations[g_portalPhase]) {
        g_phaseFrame = 0;
        g_portalPhase = (g_portalPhase + 1) % 8;
        if (g_portalPhase == 0) g_frame = 0; // restart the perimeter walk cleanly
    }
}

void ResetAnimation() {
    g_pos = 0; g_dir = 1; g_wait = 0;
    g_frame = 0;
    g_portalPhase = 0; g_phaseFrame = 0;
}

void RenderLoop(int fps) {
    const auto period = std::chrono::milliseconds(1000 / (fps > 0 ? fps : 30));
    while (!g_stop.load()) {
        auto t0 = std::chrono::steady_clock::now();
        RenderFrame();
        auto dt = std::chrono::steady_clock::now() - t0;
        if (dt < period) std::this_thread::sleep_for(period - dt);
    }
}

} // namespace

// ------------------------------------------------------------------ state API
void TaskBegin() { g_tasks.fetch_add(1); }

void TaskEnd() {
    int v = g_tasks.fetch_sub(1) - 1;
    if (v < 0) g_tasks.store(0);
}

void TaskReset() { g_tasks.store(0); }

void NotifyChat() { g_lastChatMs.store(NowMs()); }

int ActiveTasks() { return g_tasks.load(); }

State CurrentState() {
    long long last = g_lastChatMs.load();
    bool chatActive = (last != 0) && ((NowMs() - last) < g_chatHoldMs.load());
    if (chatActive) return State::Portal;
    int n = g_tasks.load();
    if (n >= g_portalMin.load()) return State::Portal;
    if (n >= g_busyMin.load())   return State::Busy;
    return State::Idle;
}

const char* StateName(State s) {
    switch (s) {
    case State::Idle:   return "空闲";
    case State::Busy:   return "忙碌";
    default:            return "崩溃";
    }
}

// ----------------------------------------------------------------- lifecycle
bool ConsoleInit() {
    // Captures the console, reserves the fixed header region, and installs the
    // std::cout sink so log text lands in the pane below instead of being
    // written over the animated header. Returns false when there is no real
    // console (stdout redirected/absent) so the caller can skip the UI rather
    // than emit raw escape codes into the output stream.
    if (!VectorConsole::Init(14)) return false;  // 14 rows = box frame height
    VectorConsole::InstallLoggerSink();
    return true;
}

void ConsoleShutdown() {
    VectorConsole::RemoveLoggerSink();
    VectorConsole::Shutdown();
}

bool Start(int fps) {
    if (g_running.load()) return true;
    if (fps <= 0) fps = 30;
    g_fps.store(fps);
    g_stop.store(false);
    ResetAnimation();
    g_lastState = CurrentState();
    g_started = true;
    g_running.store(true);
    g_thread = std::thread(RenderLoop, fps);
    return true;
}

void Stop() {
    if (!g_running.load()) return;
    g_stop.store(true);
    if (g_thread.joinable()) g_thread.join();
    g_running.store(false);
}

bool IsRunning() { return g_running.load(); }

void RenderFrame() {
    State st = CurrentState();
    if (!g_started) { g_lastState = st; g_started = true; }

    if (st != g_lastState) {
        ResetAnimation();
        g_lastState = st;
        WriteOut(std::string(kClear)); // avoid residue from the previous layout
    }

    switch (st) {
    case State::Idle:   DrawIdle();   break;
    case State::Busy:   DrawBusy();   break;
    default:            DrawPortal(); break;
    }
    ++g_frame;
}

// ------------------------------------------------------------------ settings
void SetStateThresholds(int busyMin, int portalMin) {
    if (busyMin < 1) busyMin = 1;
    if (portalMin <= busyMin) portalMin = busyMin + 1;
    g_busyMin.store(busyMin);
    g_portalMin.store(portalMin);
}

void SetChatHoldMs(long long ms) {
    g_chatHoldMs.store(ms > 0 ? ms : 0);
}

} // namespace VectorUI
