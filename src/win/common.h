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

} // namespace ln
