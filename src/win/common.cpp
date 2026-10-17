#include "common.h"
#include <shlobj.h>
#include <cstdarg>
#include <cstdio>

namespace ln {

HINSTANCE g_hInst = nullptr;
volatile LONG g_lockCount = 0;

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring dataDir() {
    wchar_t buf[MAX_PATH] = L"";
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, buf)))
        GetTempPathW(MAX_PATH, buf);
    std::wstring d = buf;
    if (!d.empty() && d.back() != L'\\') d += L'\\';
    d += L"LinerNotes\\";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

std::wstring cacheDir() {
    std::wstring d = dataDir() + L"cache\\";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

std::wstring logPath() { return dataDir() + L"linernotes.log"; }

static CRITICAL_SECTION* logLock() {
    static CRITICAL_SECTION cs;
    static LONG init = 0;
    if (InterlockedCompareExchange(&init, 1, 0) == 0) { InitializeCriticalSection(&cs); InterlockedExchange(&init, 2); }
    while (init != 2) Sleep(0);
    return &cs;
}

static DWORD g_wmpThread = 0;
static double g_wmpLogMs = 0, g_ourWmpMs = 0;   // touched on WMP's thread only
static volatile LONG g_phase = LONG(Phase::Idle);

double nowMs() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) * 1000.0 / double(freq.QuadPart);
}
void setWmpThread() { g_wmpThread = GetCurrentThreadId(); }
bool onWmpThread() { return g_wmpThread && GetCurrentThreadId() == g_wmpThread; }
double wmpThreadLogMs() { return g_wmpLogMs; }
double ourWmpThreadMs() { return g_ourWmpMs; }

void setPhase(Phase p) { InterlockedExchange(&g_phase, LONG(p)); }
const char* phaseText() {
    switch (Phase(g_phase)) {
    case Phase::ReadingToc: return "reading the disc's table of contents";
    case Phase::AskingMusicBrainz: return "waiting on MusicBrainz";
    case Phase::AskingGnuDb: return "waiting on GnuDB";
    case Phase::Idle: break;
    }
    return "idle";
}

WmpThreadTimer::WmpThreadTimer(const char* what, bool topLevel, double thresholdMs)
    : m_what(what), m_top(topLevel), m_threshold(thresholdMs), m_t0(nowMs()), m_log0(g_wmpLogMs) {}

WmpThreadTimer::~WmpThreadTimer() {
    double took = nowMs() - m_t0, logMs = g_wmpLogMs - m_log0;
    if (m_top) g_ourWmpMs += took;
    if (took >= m_threshold)
        logf("Timing: %s took %.0f ms on WMP's thread (%.0f ms of it writing this log).", m_what, took, logMs);
}

void logLine(const std::string& msg) {
    const double t0 = nowMs();
    struct AddLogTime { double t0; ~AddLogTime() { if (onWmpThread()) g_wmpLogMs += nowMs() - t0; } } addLogTime{t0};
    EnterCriticalSection(logLock());
    std::wstring path = logPath();
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 2 * 1024 * 1024 && fa.nFileSizeHigh == 0) {
        std::wstring old = path + L".old";
        MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        SYSTEMTIME st; GetLocalTime(&st);
        char head[64];
        std::snprintf(head, sizeof head, "%04d-%02d-%02d %02d:%02d:%02d.%03d [%5lu] ", st.wYear, st.wMonth, st.wDay,
                      st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, (unsigned long)GetCurrentThreadId());
        std::string line = head + msg + "\r\n";
        DWORD w; WriteFile(h, line.data(), DWORD(line.size()), &w, nullptr);
        CloseHandle(h);
    }
    LeaveCriticalSection(logLock());
}

void logf(const char* fmt, ...) {
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    logLine(buf);
}

// ---- settings (HKCU\Software\LinerNotes) ----
static const wchar_t* kKey = L"Software\\LinerNotes";

static DWORD readDword(HKEY k, const wchar_t* name, DWORD def) {
    DWORD v = def, sz = sizeof v, type = 0;
    if (RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<BYTE*>(&v), &sz) != ERROR_SUCCESS || type != REG_DWORD) return def;
    return v;
}
static std::string readString(HKEY k, const wchar_t* name) {
    wchar_t buf[512]; DWORD sz = sizeof buf, type = 0;
    if (RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<BYTE*>(buf), &sz) != ERROR_SUCCESS || type != REG_SZ) return "";
    buf[511] = 0;
    return narrow(buf);
}

void Settings::load() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    autoApply = readDword(k, L"AutoApply", autoApply) != 0;
    promptWhenUnsure = readDword(k, L"PromptWhenUnsure", promptWhenUnsure) != 0;
    overwriteExisting = readDword(k, L"OverwriteExisting", overwriteExisting) != 0;
    useGnuDb = readDword(k, L"UseGnuDb", useGnuDb) != 0;
    verboseLog = readDword(k, L"VerboseLog", verboseLog) != 0;
    contactEmail = readString(k, L"ContactEmail");
    RegCloseKey(k);
}

void Settings::save() const {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    auto putD = [&](const wchar_t* n, bool v) { DWORD d = v ? 1 : 0; RegSetValueExW(k, n, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&d), sizeof d); };
    putD(L"AutoApply", autoApply);
    putD(L"PromptWhenUnsure", promptWhenUnsure);
    putD(L"OverwriteExisting", overwriteExisting);
    putD(L"UseGnuDb", useGnuDb);
    putD(L"VerboseLog", verboseLog);
    std::wstring e = widen(contactEmail);
    RegSetValueExW(k, L"ContactEmail", 0, REG_SZ, reinterpret_cast<const BYTE*>(e.c_str()), DWORD((e.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

static CRITICAL_SECTION* settingsLock() {
    static CRITICAL_SECTION cs;
    static bool init = [] { InitializeCriticalSection(&cs); return true; }();
    (void)init;
    return &cs;
}
static Settings g_settings;
static bool g_settingsLoaded = false;

Settings settings() {
    EnterCriticalSection(settingsLock());
    if (!g_settingsLoaded) { g_settings.load(); g_settingsLoaded = true; }
    Settings copy = g_settings;
    LeaveCriticalSection(settingsLock());
    return copy;
}

void updateSettings(const Settings& s) {
    EnterCriticalSection(settingsLock());
    g_settings = s;
    g_settingsLoaded = true;
    LeaveCriticalSection(settingsLock());
    s.save();
}

std::string userCountryCode() {
    wchar_t buf[16] = L"";
    if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, buf, 16) > 0) return narrow(buf);
    return "";
}

// ---- worker threads that pin the module ----
struct WorkerStart { void (*fn)(void*); void* arg; HMODULE mod; };

static DWORD WINAPI workerThunk(LPVOID p) {
    WorkerStart ws = *static_cast<WorkerStart*>(p);
    delete static_cast<WorkerStart*>(p);
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ws.fn(ws.arg);
    if (SUCCEEDED(hr)) CoUninitialize();
    InterlockedDecrement(&g_lockCount);
    FreeLibraryAndExitThread(ws.mod, 0);
    return 0;
}

bool startWorker(void (*fn)(void*), void* arg) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&startWorker), &mod)) return false;
    auto* ws = new WorkerStart{fn, arg, mod};
    InterlockedIncrement(&g_lockCount);
    HANDLE h = CreateThread(nullptr, 0, workerThunk, ws, 0, nullptr);
    if (!h) { delete ws; InterlockedDecrement(&g_lockCount); FreeLibrary(mod); return false; }
    CloseHandle(h);
    return true;
}

} // namespace ln
