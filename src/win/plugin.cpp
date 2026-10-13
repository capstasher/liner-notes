#include "plugin.h"
#include "common.h"
#include "dialog.h"
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace ln {

namespace {
// {19a6627b-da9e-47c1-bb23-00b5e668236a}
const IID kIID_IWMPEvents = {0x19a6627b, 0xda9e, 0x47c1, {0xbb, 0x23, 0x00, 0xb5, 0xe6, 0x68, 0x23, 0x6a}};
const IID kIID_IWMPMedia = {0x94d55e95, 0x3fac, 0x11d3, {0xb1, 0x55, 0x00, 0xc0, 0x4f, 0x79, 0xfa, 0xa6}};
// {4C5E8F9F-AD3E-4bf9-9753-FCD30D6D38DD}
const IID kIID_IWMPPluginUI = {0x4c5e8f9f, 0xad3e, 0x4bf9, {0x97, 0x53, 0xfc, 0xd3, 0x0d, 0x6d, 0x38, 0xdd}};

const UINT WM_APP_LOOKUP_DONE = WM_APP + 1;
const UINT WM_APP_OPEN_DIALOG = WM_APP + 2;
const UINT_PTR TIMER_INITIAL = 1;
const UINT_PTR TIMER_SCAN_BASE = 100;     // + drive index
const UINT_PTR TIMER_VERIFY_BASE = 300;   // + drive index
const wchar_t* kMsgClass = L"LinerNotesMsgWnd";

struct Bstr {
    BSTR b;
    explicit Bstr(const std::wstring& s) : b(SysAllocString(s.c_str())) {}
    Bstr() : b(nullptr) {}
    ~Bstr() { SysFreeString(b); }
    std::string utf8() const { return b ? narrow(std::wstring(b, SysStringLen(b))) : std::string(); }
};

template <class T> void safeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

std::string getInfo(IWMPMedia* m, const wchar_t* name) {
    Bstr n(name), v;
    if (FAILED(m->getItemInfo(n.b, &v.b))) return "";
    return v.utf8();
}

} // namespace

struct LookupPayload {
    int idx;
    int generation;
    bool useCache;
    HWND target;
    DiscLookup lookup;
};

static void lookupWorker(void* arg) {
    auto* p = static_cast<LookupPayload*>(arg);
    lookupDisc(p->lookup, p->useCache);
    if (!PostMessageW(p->target, WM_APP_LOOKUP_DONE, WPARAM(p->idx), LPARAM(p))) delete p;  // plug-in gone
}

// --------------------------------------------------------------------- lifetime

Plugin::Plugin() { InterlockedIncrement(&g_lockCount); }
Plugin::~Plugin() { shutdown(); InterlockedDecrement(&g_lockCount); }

STDMETHODIMP Plugin::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIID_IWMPPluginUI)) *ppv = static_cast<IWMPPluginUI*>(this);
    else if (IsEqualIID(riid, kIID_IWMPEvents)) *ppv = static_cast<IWMPEvents*>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef();
    return S_OK;
}
STDMETHODIMP_(ULONG) Plugin::AddRef() { return ULONG(InterlockedIncrement(&m_ref)); }
STDMETHODIMP_(ULONG) Plugin::Release() {
    LONG r = InterlockedDecrement(&m_ref);
    if (r == 0) delete this;
    return ULONG(r);
}

STDMETHODIMP Plugin::SetCore(IWMPCore* pCore) {
    if (!pCore) { logLine("SetCore(NULL): shutting down."); shutdown(); return S_OK; }
    shutdown();
    { Settings fresh; fresh.load(); updateSettings(fresh); }
    logf("%s here! LinerNotes %s starting (%s build).", noto::name().c_str(), LINERNOTES_VERSION,
         sizeof(void*) == 8 ? "64-bit" : "32-bit");
    m_core = pCore;
    m_core->AddRef();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = MsgWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = kMsgClass;
    RegisterClassExW(&wc);   // fails harmlessly if already registered
    m_hwnd = CreateWindowExW(0, kMsgClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hInst, nullptr);
    if (m_hwnd) SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, LONG_PTR(this));

    IConnectionPointContainer* cpc = nullptr;
    if (SUCCEEDED(m_core->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(&cpc)))) {
        if (SUCCEEDED(cpc->FindConnectionPoint(kIID_IWMPEvents, &m_cp))) {
            HRESULT hr = m_cp->Advise(static_cast<IWMPEvents*>(this), &m_cookie);
            if (FAILED(hr)) { logf("Advise failed: 0x%08lx", hr); safeRelease(m_cp); }
        } else logLine("No IWMPEvents connection point on the core.");
        cpc->Release();
    }
    if (m_hwnd) SetTimer(m_hwnd, TIMER_INITIAL, 2500, nullptr);   // a disc may already be in the drive
    return S_OK;
}

void Plugin::shutdown() {
    if (m_cp) { m_cp->Unadvise(m_cookie); safeRelease(m_cp); m_cookie = 0; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }   // in-flight results then fail to post and free themselves
    safeRelease(m_core);
    m_drives.clear();
}

// --------------------------------------------------------------------- message window

LRESULT CALLBACK Plugin::MsgWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Plugin*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) {
        if (msg == WM_TIMER) { self->AddRef(); self->onTimer(UINT_PTR(wp)); self->Release(); return 0; }
        if (msg == WM_APP_LOOKUP_DONE) { self->AddRef(); self->onLookupDone(int(wp), reinterpret_cast<LookupPayload*>(lp)); self->Release(); return 0; }
        if (msg == WM_APP_OPEN_DIALOG) { self->AddRef(); self->openDialog(int(wp), nullptr); self->Release(); return 0; }
    } else if (msg == WM_APP_LOOKUP_DONE) {
        delete reinterpret_cast<LookupPayload*>(lp);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void Plugin::onTimer(UINT_PTR id) {
    KillTimer(m_hwnd, id);
    if (id == TIMER_INITIAL) scanAllDrives();
    else if (id >= TIMER_SCAN_BASE && id < TIMER_SCAN_BASE + 26) scanDrive(int(id - TIMER_SCAN_BASE));
    else if (id >= TIMER_VERIFY_BASE && id < TIMER_VERIFY_BASE + 26) verify(int(id - TIMER_VERIFY_BASE));
}

// --------------------------------------------------------------------- CD handling

void Plugin::CdromMediaChange(long num) {
    if (!m_hwnd || num < 0 || num > 25) return;
    logf("CdromMediaChange(%ld)", num);
    DriveState& st = m_drives[int(num)];
    st.generation++;           // invalidates any lookup still running for the previous disc
    st.busy = false;
    st.waitTries = 0;
    st.haveLookup = false;
    st.hasApplied = false;
    st.status = "Reading disc...";
    st.say = noto::reading();
    KillTimer(m_hwnd, TIMER_VERIFY_BASE + num);
    SetTimer(m_hwnd, TIMER_SCAN_BASE + UINT_PTR(num), 1500, nullptr);
    notifyDialog();
}

bool Plugin::getPlaylist(int idx, IWMPPlaylist** pl, std::wstring* drive) {
    *pl = nullptr;
    if (!m_core) return false;
    IWMPCdromCollection* coll = nullptr;
    if (FAILED(m_core->get_cdromCollection(&coll)) || !coll) return false;
    IWMPCdrom* cd = nullptr;
    HRESULT hr = coll->WMP_CDCOLL_ITEM(idx, &cd);
    coll->Release();
    if (FAILED(hr) || !cd) return false;
    if (drive) { Bstr d; if (SUCCEEDED(cd->get_driveSpecifier(&d.b)) && d.b) *drive = d.b; }
    hr = cd->WMP_CDROM_PLAYLIST(pl);
    cd->Release();
    return SUCCEEDED(hr) && *pl;
}

int Plugin::playlistTrackCount(int idx) {
    IWMPPlaylist* pl = nullptr;
    long n = 0;
    if (getPlaylist(idx, &pl, nullptr)) { pl->get_count(&n); pl->Release(); }
    return int(n);
}

void Plugin::scanAllDrives() {
    if (!m_core) return;
    IWMPCdromCollection* coll = nullptr;
    long n = 0;
    if (SUCCEEDED(m_core->get_cdromCollection(&coll)) && coll) { coll->get_count(&n); coll->Release(); }
    logf("Found %ld optical drive(s).", n);
    for (long i = 0; i < n && i < 26; ++i) scanDrive(int(i));   // polls briefly; empty drives settle quietly
}

void Plugin::scanDrive(int idx) {
    DriveState& st = m_drives[idx];
    IWMPPlaylist* pl = nullptr;
    std::wstring drive;
    long count = 0;
    if (getPlaylist(idx, &pl, &drive)) { pl->get_count(&count); pl->Release(); }
    if (!drive.empty()) st.drive = drive;
    if (count <= 0) {
        // WMP fills the CD playlist asynchronously; an empty drive never fills it.
        if (++st.waitTries <= 8) { SetTimer(m_hwnd, TIMER_SCAN_BASE + idx, 1500, nullptr); return; }
        st.status = "No audio CD in " + narrow(st.drive) + ".";
        st.say = noto::noAudioCd(narrow(st.drive));
        st.haveLookup = false;
        notifyDialog();
        return;
    }
    if (st.busy) return;
    m_lastDrive = idx;
    startLookup(idx, true);
}

void Plugin::startLookup(int idx, bool useCache) {
    DriveState& st = m_drives[idx];
    if (st.drive.empty()) { IWMPPlaylist* pl = nullptr; if (getPlaylist(idx, &pl, &st.drive)) pl->Release(); }
    if (st.drive.empty() || !m_hwnd) return;
    auto* p = new LookupPayload{idx, st.generation, useCache, m_hwnd, DiscLookup()};
    p->lookup.drive = st.drive;
    st.busy = true;
    st.status = "Looking up disc online...";
    st.say = noto::lookingUp();
    notifyDialog();
    if (!startWorker(lookupWorker, p)) {
        delete p;
        st.busy = false;
        st.status = "Could not start lookup thread.";
        st.say = noto::threadFailed();
    }
}

void Plugin::relookup(int idx, bool useCache) {
    DriveState& st = m_drives[idx];
    if (st.busy) return;
    if (!useCache && st.haveLookup) forgetCache(st.lookup.mbDiscId);
    st.generation++;
    startLookup(idx, useCache);
}

void Plugin::onLookupDone(int idx, LookupPayload* p) {
    DriveState& st = m_drives[idx];
    if (p->generation != st.generation) { logLine("Dropping stale lookup result."); delete p; return; }
    st.busy = false;
    st.lookup = p->lookup;
    st.haveLookup = true;
    st.status = st.lookup.status;
    switch (st.lookup.outcome) {
    case LookupOutcome::TocFailed:  st.say = noto::tocFailed(); break;
    case LookupOutcome::Cached:
        st.say = noto::remembered(st.lookup.candidates.empty() ? "" : st.lookup.candidates[0].displayTitle());
        break;
    case LookupOutcome::Found:
        if (st.lookup.candidates.size() == 1 || candidatesEquivalent(st.lookup.candidates))
            st.say = noto::foundOne(st.lookup.candidates[0].displayTitle(), st.lookup.candidates[0].artist);
        else
            st.say = noto::foundMany(int(st.lookup.candidates.size()));
        break;
    case LookupOutcome::NetProblem: st.say = noto::offline(); break;
    case LookupOutcome::NotFound:
    case LookupOutcome::None:       st.say = noto::notFound(); break;
    }
    delete p;
    int wmpCount = playlistTrackCount(idx);
    int tocCount = st.lookup.tocOk ? st.lookup.toc.audioTrackCount() : -1;
    if (tocCount >= 0 && wmpCount != tocCount)
        logf("Note: WMP lists %d tracks but the TOC has %d audio tracks.", wmpCount, tocCount);
    decide(idx);
    notifyDialog();
}

void Plugin::decide(int idx) {
    DriveState& st = m_drives[idx];
    const Settings s = settings();
    const auto& c = st.lookup.candidates;
    std::string msg;
    if (c.empty()) {
        if (s.promptWhenUnsure && st.lookup.tocOk) PostMessageW(m_hwnd, WM_APP_OPEN_DIALOG, WPARAM(idx), 0);
        return;
    }
    bool clear = st.lookup.fromCache || c.size() == 1 || candidatesEquivalent(c);
    if (clear && (s.autoApply || st.lookup.fromCache)) {
        applyAlbum(idx, c[0], false, msg);
        st.status = msg;
    } else if (s.promptWhenUnsure) {
        PostMessageW(m_hwnd, WM_APP_OPEN_DIALOG, WPARAM(idx), 0);
    } else if (s.autoApply) {
        applyAlbum(idx, c[0], false, msg);   // user opted out of prompts: take the best-ranked match
        st.status = msg;
    }
}

// --------------------------------------------------------------------- writing to WMP

void Plugin::dumpAttributes(IWMPMedia* m, IWMPPlaylist* pl) {
    long n = 0;
    if (m && SUCCEEDED(m->get_attributeCount(&n))) {
        logf("Attribute dump of CD track 1 (%ld attributes):", n);
        for (long i = 0; i < n; ++i) {
            Bstr name;
            if (FAILED(m->getAttributeName(i, &name.b)) || !name.b) continue;
            Bstr val;
            m->getItemInfo(name.b, &val.b);
            VARIANT_BOOL ro = VARIANT_TRUE;
            m->isReadOnlyItem(name.b, &ro);
            logf("  %-28s %s = %.120s", name.utf8().c_str(), ro ? "(ro)" : "(rw)", val.utf8().c_str());
        }
    }
    n = 0;
    if (pl && SUCCEEDED(pl->get_attributeCount(&n))) {
        logf("Attribute dump of the CD playlist (%ld attributes):", n);
        for (long i = 0; i < n; ++i) {
            Bstr name;
            if (FAILED(pl->get_attributeName(i, &name.b)) || !name.b) continue;
            Bstr val;
            pl->getItemInfo(name.b, &val.b);
            logf("  %-28s = %.120s", name.utf8().c_str(), val.utf8().c_str());
        }
    }
}

// WMP 10+ ignores WM/Year; the year lives in ReleaseDate (a date), with ReleaseDateYear derived
// from it. Which string spelling WMP accepts isn't documented, so probe on track 1, read back,
// and remember the winner for the session.
bool Plugin::writeYear(const std::vector<IWMPMedia*>& items, const std::string& y, std::string& dateValue) {
    struct Way { const wchar_t* attr; std::string value; };
    // ReleaseDate is the stored value; ReleaseDateYear is derived from it, so it's the last resort
    // (v0.1 wrote it first, and WMP still showed an unknown year).
    const Way ways[] = {{L"ReleaseDate", "1/1/" + y}, {L"ReleaseDate", y + "-01-01"}, {L"ReleaseDate", "7/7/" + y},
                        {L"ReleaseDate", y}, {L"ReleaseDateYear", y}};
    const int nWays = int(sizeof ways / sizeof ways[0]);
    IWMPMedia* first = items.front();
    auto set = [](IWMPMedia* m, const Way& w) {
        Bstr n(w.attr);
        VARIANT_BOOL ro = VARIANT_FALSE;
        if (SUCCEEDED(m->isReadOnlyItem(n.b, &ro)) && ro) return false;
        Bstr v(widen(w.value));
        return SUCCEEDED(m->setItemInfo(n.b, v.b));
    };
    int winner = -1;
    for (int k = -1; k < nWays && winner < 0; ++k) {
        int i = (k == -1) ? m_yearMethod : k;      // remembered method first
        if (i < 0 || (k >= 0 && i == m_yearMethod)) continue;
        if (!set(first, ways[i])) { if (settings().verboseLog) logf("Year: %s refused.", narrow(ways[i].attr).c_str()); continue; }
        std::string gotYear = getInfo(first, L"ReleaseDateYear"), gotDate = getInfo(first, L"ReleaseDate");
        if (settings().verboseLog)
            logf("Year: %s=\"%s\" -> ReleaseDateYear \"%s\", ReleaseDate \"%s\"", narrow(ways[i].attr).c_str(),
                 ways[i].value.c_str(), gotYear.c_str(), gotDate.c_str());
        // A bare "1999" echoed back unparsed proves nothing, so that spelling needs ReleaseDateYear to agree.
        if (gotYear == y || (gotYear.empty() && ways[i].value != y && gotDate.find(y) != std::string::npos)) winner = i;
    }
    int use = winner >= 0 ? winner : 0;              // unverified: 1/1/YYYY reads the same in d/m and m/d locales
    if (winner >= 0) m_yearMethod = winner;
    else logf("Year %s could not be verified on track 1; writing ReleaseDate=\"%s\" anyway.", y.c_str(), ways[use].value.c_str());
    for (size_t i = 0; i < items.size(); ++i)
        if (i > 0 || winner < 0) set(items[i], ways[use]);
    dateValue = std::wcscmp(ways[use].attr, L"ReleaseDate") == 0 ? ways[use].value : "1/1/" + y;
    return winner >= 0;
}

bool Plugin::applyAlbum(int idx, const AlbumMeta& album, bool userInitiated, std::string& message) {
    DriveState& st = m_drives[idx];
    IWMPPlaylist* pl = nullptr;
    if (!getPlaylist(idx, &pl, nullptr)) { message = "The CD is no longer available."; st.say = noto::cdGone(); return false; }
    long count = 0;
    pl->get_count(&count);
    if (count <= 0) { pl->Release(); message = "WMP shows no tracks for this CD."; st.say = noto::noTracksYet(); return false; }

    std::vector<IWMPMedia*> items;
    std::vector<std::string> currentTitles;
    for (long i = 0; i < count; ++i) {
        IWMPMedia* m = nullptr;
        if (SUCCEEDED(pl->WMP_PLAYLIST_ITEM(i, &m)) && m) { items.push_back(m); currentTitles.push_back(getInfo(m, L"Title")); }
    }
    const Settings cfg = settings();
    if (!items.empty() && cfg.verboseLog && !m_dumped) { dumpAttributes(items[0], pl); m_dumped = true; }

    bool placeholders = looksLikePlaceholderTitles(currentTitles);
    if (!userInitiated && !placeholders && !cfg.overwriteExisting) {
        for (auto* m : items) m->Release();
        pl->Release();
        message = "WMP already has titles for this disc; left them alone.";
        st.say = noto::leftAlone();
        logLine(message);
        return false;
    }
    if (int(items.size()) != int(album.tracks.size()))
        logf("Track count differs: WMP %d vs %s %d. Mapping by track number.", int(items.size()), album.source.c_str(), int(album.tracks.size()));

    int written = 0, refused = 0;
    std::string refusedNames;
    auto put = [&](IWMPMedia* m, const wchar_t* name, const std::string& value) {
        if (value.empty()) return false;
        Bstr n(name);
        VARIANT_BOOL ro = VARIANT_FALSE;
        if (SUCCEEDED(m->isReadOnlyItem(n.b, &ro)) && ro) {
            ++refused;
            if (refusedNames.find(narrow(name)) == std::string::npos) refusedNames += narrow(name) + " ";
            return false;
        }
        Bstr v(widen(value));
        HRESULT hr = m->setItemInfo(n.b, v.b);
        if (FAILED(hr)) { ++refused; logf("setItemInfo(%s) failed: 0x%08lx", narrow(name).c_str(), hr); return false; }
        ++written;
        return true;
    };

    for (size_t i = 0; i < items.size(); ++i) {
        IWMPMedia* m = items[i];
        int number = std::atoi(getInfo(m, L"WM/TrackNumber").c_str());
        size_t t = (number >= 1 && number <= int(album.tracks.size())) ? size_t(number - 1) : i;
        if (t < album.tracks.size()) {
            put(m, L"Title", album.tracks[t].title);
            put(m, L"Author", album.trackArtist(t));
        }
        put(m, L"WM/AlbumTitle", album.displayTitle());
        put(m, L"WM/AlbumArtist", album.artist);
        put(m, L"WM/Genre", album.genre);
        put(m, L"WM/Publisher", album.label);
    }

    std::string year = album.originalYear(), dateValue;
    bool yearOk = false;
    if (!year.empty() && !items.empty()) yearOk = writeYear(items, year, dateValue);

    // Album-level attributes of the CD playlist itself (documented for CD playlists; WMP's
    // CD view may read the header from here rather than from the tracks).
    auto plPut = [&](const wchar_t* name, const std::string& value) {
        if (value.empty()) return;
        Bstr n(name), v(widen(value));
        HRESULT hr = pl->setItemInfo(n.b, v.b);
        if (FAILED(hr) && cfg.verboseLog) logf("CD playlist %s refused: 0x%08lx", narrow(name).c_str(), hr);
    };
    { Bstr nm(widen(album.displayTitle())); HRESULT hr = pl->put_name(nm.b); if (FAILED(hr) && cfg.verboseLog) logf("CD playlist name refused: 0x%08lx", hr); }
    plPut(L"Author", album.artist);
    plPut(L"WM/Genre", album.genre);
    plPut(L"WM/Publisher", album.label);
    plPut(L"ReleaseDate", dateValue);

    for (auto* m : items) m->Release();
    std::string check;
    { IWMPMedia* m = nullptr; if (SUCCEEDED(pl->WMP_PLAYLIST_ITEM(0, &m)) && m) { check = getInfo(m, L"Title"); m->Release(); } }
    pl->Release();

    logf("Applied \"%s\" by %s [%s %s]: %d values written, %d refused%s%s; year %s%s; track 1 now reads \"%s\".",
         album.displayTitle().c_str(), album.artist.c_str(), album.source.c_str(), album.id.c_str(),
         written, refused, refusedNames.empty() ? "" : " (read-only: ", refusedNames.empty() ? "" : (refusedNames + ")").c_str(),
         year.empty() ? "unknown" : year.c_str(), year.empty() ? "" : (yearOk ? " (verified)" : " (unverified)"), check.c_str());

    st.applied = album;
    st.hasApplied = written > 0;
    st.verifyCount = 0;
    if (st.hasApplied) {
        if (st.haveLookup) saveToCache(st.lookup.mbDiscId, album);
        SetTimer(m_hwnd, TIMER_VERIFY_BASE + idx, 4000, nullptr);
        message = "Applied: " + album.displayTitle() + " - " + album.artist;
        st.say = noto::applied(album.displayTitle(), album.artist);
    } else {
        message = "WMP refused the new details (see log).";
        st.say = noto::refused();
    }
    notifyDialog();
    return st.hasApplied;
}

void Plugin::verify(int idx) {
    // WMP's own (now dead) online lookup can reset titles after we write them.
    DriveState& st = m_drives[idx];
    if (!st.hasApplied) return;
    IWMPPlaylist* pl = nullptr;
    if (!getPlaylist(idx, &pl, nullptr)) return;
    std::vector<std::string> titles;
    long count = 0;
    pl->get_count(&count);
    for (long i = 0; i < count; ++i) {
        IWMPMedia* m = nullptr;
        if (SUCCEEDED(pl->WMP_PLAYLIST_ITEM(i, &m)) && m) { titles.push_back(getInfo(m, L"Title")); m->Release(); }
    }
    pl->Release();
    if (!looksLikePlaceholderTitles(titles)) return;     // our titles stuck (or the user edited them)
    if (++st.verifyCount > 3) { logLine("Titles keep reverting; giving up re-applying."); return; }
    logf("Titles reverted to placeholders; re-applying (attempt %d).", st.verifyCount);
    AlbumMeta a = st.applied;
    int attempt = st.verifyCount;
    std::string msg;
    applyAlbum(idx, a, true, msg);
    m_drives[idx].verifyCount = attempt;
    if (attempt < 3) SetTimer(m_hwnd, TIMER_VERIFY_BASE + idx, 4000u * UINT(attempt + 1), nullptr);
}

// --------------------------------------------------------------------- dialog glue

int Plugin::activeDrive() const {
    if (m_lastDrive >= 0) return m_lastDrive;
    return m_drives.empty() ? 0 : m_drives.begin()->first;
}

DriveState* Plugin::driveState(int idx) {
    auto it = m_drives.find(idx);
    return it == m_drives.end() ? nullptr : &it->second;
}

void Plugin::notifyDialog() { if (m_hDlg && IsWindow(m_hDlg)) PostMessageW(m_hDlg, WM_APP_DIALOG_REFRESH, 0, 0); }

namespace {
struct FindCtx { DWORD pid; HWND best; };
BOOL CALLBACK findPlayerProc(HWND h, LPARAM lp) {
    auto* c = reinterpret_cast<FindCtx*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != c->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    wchar_t cls[64] = L"";
    GetClassNameW(h, cls, 64);
    if (wcscmp(cls, L"WMPlayerApp") == 0) { c->best = h; return FALSE; }
    if (!c->best) c->best = h;
    return TRUE;
}
} // namespace

HWND Plugin::findPlayerWindow() {
    FindCtx ctx = {GetCurrentProcessId(), nullptr};
    EnumWindows(findPlayerProc, LPARAM(&ctx));
    return ctx.best;
}

void Plugin::openDialog(int idx, HWND owner) {
    if (m_hDlg && IsWindow(m_hDlg)) { notifyDialog(); SetForegroundWindow(m_hDlg); return; }
    if (!owner) owner = findPlayerWindow();
    AddRef();
    runLinerNotesDialog(this, owner, idx);
    Release();
}

STDMETHODIMP Plugin::DisplayPropertyPage(HWND hwndParent) {
    openDialog(activeDrive(), hwndParent);
    return S_OK;
}

} // namespace ln
