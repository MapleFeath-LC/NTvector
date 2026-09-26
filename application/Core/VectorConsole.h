#pragma once
// VectorConsole - Windows console output layer for VectorUI.
//
// Two responsibilities:
//
//  1) Output setup (always on): put the console into UTF-8 + VT mode so the
//     application's UTF-8 string literals (see /utf-8 in CMakeLists) render as
//     real text and Logger's ANSI colour codes are interpreted instead of being
//     printed literally as "[0m".
//
//  2) Two-region TUI (only with --ui): reserve a fixed header of `uiRows` rows
//     that is redrawn in place, plus a log pane in the rows below it. The log
//     pane keeps its own history and scrolls inside its own rectangle, so new
//     log lines never move the header.
//
// All drawing uses WriteConsoleW / SetConsoleCursorPosition /
// SetConsoleTextAttribute, so it does not depend on the code page or on ANSI
// support. When stdout is not a console (redirected to a file or pipe) the
// functions transparently fall back to writing UTF-8/ANSI bytes.

#include <string>

namespace VectorConsole {

// --- output setup (always on) ----------------------------------------------
bool SetupOutput();      // UTF-8 code page + ENABLE_VIRTUAL_TERMINAL_PROCESSING
void RestoreOutput();    // restores the previous code page and mode

// --- two-region TUI --------------------------------------------------------
bool Init(int uiRows);   // capture the console, reserve the header region
void Shutdown();
bool Active();           // true when the TUI owns the console
bool IsConsole();        // true when stdout is a real console

void SetUiRows(int rows);
int  UiRows();
int  LogRows();

// Draws one header frame. `ansiFrame` is the same escape-coded string that the
// file/fallback path writes, so the drawing code is shared.
void Present(const std::string& ansiFrame);

// Log pane.
void AppendLog(const std::string& utf8Line);
void ScrollLog(int deltaLines);
int  LogScrollOffset();

// --- logger integration ----------------------------------------------------
void InstallLoggerSink();
void RemoveLoggerSink();
bool LoggerSinkInstalled();

// --- diagnostics -----------------------------------------------------------
// Writes the real console state (is-console flag, window vs buffer geometry,
// viewport offset, region sizes, counters) to a text file. Used to debug
// layout problems on machines where the UI cannot be observed directly.
void DumpDiagnostics(const std::string& path);

} // namespace VectorConsole
