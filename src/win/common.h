// Shared Windows helpers: string conversion, logging, settings, paths.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

namespace ln {

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& utf16);

// %LOCALAPPDATA%\LinerNotes (created on demand), with trailing backslash
std::wstring dataDir();
std::wstring cacheDir();

void logLine(const std::string& msg);          // appends a timestamped line to linernotes.log
void logf(const char* fmt, ...);
std::wstring logPath();

struct Settings {
    bool autoApply = true;         // apply clear matches without asking
    bool promptWhenUnsure = true;  // open the picker for ambiguous / unknown discs
    bool overwriteExisting = false;
    bool useGnuDb = true;          // only effective when contactEmail is set
    bool verboseLog = true;
    std::string contactEmail;      // sent in the MusicBrainz User-Agent and GnuDB hello
    void load();
    void save() const;
};
// Thread-safe: workers read snapshots, the UI thread writes whole structs.
Settings settings();
void updateSettings(const Settings& s);   // stores and persists to HKCU

std::string userCountryCode();  // e.g. "GB", from the Windows region setting
extern HINSTANCE g_hInst;
extern volatile LONG g_lockCount;  // outstanding objects/locks for DllCanUnloadNow

// Worker thread that keeps the DLL loaded until it finishes.
bool startWorker(void (*fn)(void*), void* arg);

// ---- timing: anything that holds up WMP's own thread shows up in the log ----
double nowMs();                         // monotonic milliseconds
void setWmpThread();                    // call on WMP's thread (SetCore)
bool onWmpThread();
double wmpThreadLogMs();                // total time WMP's thread has spent writing our log

// What the worker threads are doing, so a stall report can say whether we were busy.
enum class Phase : LONG { Idle, ReadingToc, AskingMusicBrainz, AskingGnuDb };
void setPhase(Phase p);
const char* phaseText();

// Times a piece of LinerNotes' own work on WMP's thread and logs it when it takes longer
// than `thresholdMs`. Top-level timers also add to ourWmpThreadMs(), which the stall
// watchdog subtracts so it can tell WMP's own pauses from ours.
class WmpThreadTimer {
public:
    explicit WmpThreadTimer(const char* what, bool topLevel = false, double thresholdMs = 100);
    ~WmpThreadTimer();
private:
    const char* m_what;
    bool m_top;
    double m_threshold, m_t0, m_log0;
};
double ourWmpThreadMs();

} // namespace ln
