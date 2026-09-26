#pragma once
// VectorUI - terminal UI ported from NTVector.py into the main executable.
//
// Original Python UI (box frame + ASCII title + pacing mascot + portal sequence)
// has been translated to C++ and is driven by live program state instead of a
// fixed demo script:
//
//   Idle   - no tasks, no chat activity   -> mascot paces left/right (fast)
//   Busy   - 1..2 concurrent tasks        -> mascot paces left/right (slow)
//   Portal - chat invoked OR >=3 tasks    -> portal sequence (loops)
//
// Rendering happens on its own thread at 30 fps; all state inputs are
// thread-safe and may be called from any thread (including the Python
// interpreter thread).

#include <string>

namespace VectorUI {

enum class State {
    Idle = 0,
    Busy = 1,
    Portal = 2
};

// ---------------------------------------------------------------- state input
// A "task" is an in-flight unit of work dispatched by the engine (event
// callbacks, command/API invocations). Always pair TaskBegin with TaskEnd.
void TaskBegin();
void TaskEnd();
void TaskReset();

// Marks chat-box activity (incoming on_text, or an outgoing message).
void NotifyChat();

int   ActiveTasks();
State CurrentState();
const char* StateName(State s);

// ------------------------------------------------------------------ lifecycle
// Takes over the console: fixed header region + log pane. Returns false when
// there is no real console (stdout redirected), in which case the caller must
// not start the UI.
// mouseScroll: capture the mouse for wheel scrolling. Default false so the
// console's own drag-select and right-click copy keep working (F7 toggles it).
bool ConsoleInit(bool mouseScroll = false);
void ConsoleShutdown();

// Starts the render thread (no-op if already running).
bool Start(int fps = 30);
void Stop();
bool IsRunning();

// Draws exactly one frame using the current state. Safe to call directly when
// the render thread is not running (useful for tests and one-shot rendering).
void RenderFrame();

// ------------------------------------------------------------------- settings
void SetStateThresholds(int busyMin, int portalMin);
void SetChatHoldMs(long long ms);

} // namespace VectorUI
