// LinerNotes background plug-in for Windows Media Player 12.
//
// Threading: WMP creates us on its UI (STA) thread and every IWMP* call must stay
// on that thread. Network/TOC work runs on worker threads which hand results back
// by posting to a hidden message-only window owned by the WMP thread.
#pragma once
#include "wmpsdk.h"
#include "lookup.h"
#include "../core/voice.h"
#include <map>
#include <string>
#include <vector>

namespace ln {

struct DriveState {
    std::wstring drive;          // "D:"
    bool busy = false;           // lookup in flight
    int generation = 0;          // bumps on every media change; stale results are dropped
    int waitTries = 0;           // polls while WMP builds the CD playlist
    bool haveLookup = false;
    DiscLookup lookup;
    bool hasApplied = false;
    AlbumMeta applied;
    int verifyCount = 0;
    double changedAt = 0;        // nowMs() of the last CdromMediaChange, for timing WMP's track list
    std::string status;          // last status, plain (for the log)
    Line say;                    // the same moment in Noto's words, with her mood
};

class Plugin final : public IWMPPluginUI, public IWMPEvents {
public:
    Plugin();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IWMPPluginUI
    STDMETHODIMP SetCore(IWMPCore* pCore) override;
    STDMETHODIMP Create(HWND, HWND*) override { return E_NOTIMPL; }
    STDMETHODIMP Destroy() override { return S_OK; }
    STDMETHODIMP DisplayPropertyPage(HWND hwndParent) override;
    STDMETHODIMP GetProperty(const WCHAR*, VARIANT*) override { return E_NOTIMPL; }
    STDMETHODIMP SetProperty(const WCHAR*, const VARIANT*) override { return E_NOTIMPL; }
    STDMETHODIMP TranslateAccelerator(LPMSG) override { return S_FALSE; }

    // IWMPEvents — only CdromMediaChange matters; the rest are required stubs.
    void STDMETHODCALLTYPE CdromMediaChange(long num) override;
    void STDMETHODCALLTYPE OpenStateChange(long) override {}
    void STDMETHODCALLTYPE PlayStateChange(long) override {}
    void STDMETHODCALLTYPE AudioLanguageChange(long) override {}
    void STDMETHODCALLTYPE StatusChange() override {}
    void STDMETHODCALLTYPE ScriptCommand(BSTR, BSTR) override {}
    void STDMETHODCALLTYPE NewStream() override {}
    void STDMETHODCALLTYPE Disconnect(long) override {}
    void STDMETHODCALLTYPE Buffering(VARIANT_BOOL) override {}
    void STDMETHODCALLTYPE Error() override {}
    void STDMETHODCALLTYPE Warning(long, long, BSTR) override {}
    void STDMETHODCALLTYPE EndOfStream(long) override {}
    void STDMETHODCALLTYPE PositionChange(double, double) override {}
    void STDMETHODCALLTYPE MarkerHit(long) override {}
    void STDMETHODCALLTYPE DurationUnitChange(long) override {}
    void STDMETHODCALLTYPE PlaylistChange(IDispatch*, WMPPlaylistChangeEventType) override {}
    void STDMETHODCALLTYPE CurrentPlaylistChange(WMPPlaylistChangeEventType) override {}
    void STDMETHODCALLTYPE CurrentPlaylistItemAvailable(BSTR) override {}
    void STDMETHODCALLTYPE MediaChange(IDispatch*) override {}
    void STDMETHODCALLTYPE CurrentMediaItemAvailable(BSTR) override {}
    void STDMETHODCALLTYPE CurrentItemChange(IDispatch*) override {}
    void STDMETHODCALLTYPE MediaCollectionChange() override {}
    void STDMETHODCALLTYPE MediaCollectionAttributeStringAdded(BSTR, BSTR) override {}
    void STDMETHODCALLTYPE MediaCollectionAttributeStringRemoved(BSTR, BSTR) override {}
    void STDMETHODCALLTYPE MediaCollectionAttributeStringChanged(BSTR, BSTR, BSTR) override {}
    void STDMETHODCALLTYPE PlaylistCollectionChange() override {}
    void STDMETHODCALLTYPE PlaylistCollectionPlaylistAdded(BSTR) override {}
    void STDMETHODCALLTYPE PlaylistCollectionPlaylistRemoved(BSTR) override {}
    void STDMETHODCALLTYPE PlaylistCollectionPlaylistSetAsDeleted(BSTR, VARIANT_BOOL) override {}
    void STDMETHODCALLTYPE ModeChange(BSTR, VARIANT_BOOL) override {}
    void STDMETHODCALLTYPE MediaError(IDispatch*) override {}
    void STDMETHODCALLTYPE OpenPlaylistSwitch(IDispatch*) override {}
    void STDMETHODCALLTYPE DomainChange(BSTR) override {}
    void STDMETHODCALLTYPE SwitchedToPlayerApplication() override {}
    void STDMETHODCALLTYPE SwitchedToControl() override {}
    void STDMETHODCALLTYPE PlayerDockedStateChange() override {}
    void STDMETHODCALLTYPE PlayerReconnect() override {}
    void STDMETHODCALLTYPE Click(short, short, long, long) override {}
    void STDMETHODCALLTYPE DoubleClick(short, short, long, long) override {}
    void STDMETHODCALLTYPE KeyDown(short, short) override {}
    void STDMETHODCALLTYPE KeyPress(short) override {}
    void STDMETHODCALLTYPE KeyUp(short, short) override {}
    void STDMETHODCALLTYPE MouseDown(short, short, long, long) override {}
    void STDMETHODCALLTYPE MouseMove(short, short, long, long) override {}
    void STDMETHODCALLTYPE MouseUp(short, short, long, long) override {}

    // ---- used by the dialog (WMP thread only) ----
    int activeDrive() const;
    DriveState* driveState(int idx);
    int playlistTrackCount(int idx);
    bool applyAlbum(int idx, const AlbumMeta& album, bool userInitiated, std::string& message);
    void relookup(int idx, bool useCache);
    void setDialog(HWND h) { m_hDlg = h; }

private:
    ~Plugin();
    static LRESULT CALLBACK MsgWndProc(HWND, UINT, WPARAM, LPARAM);
    void onTimer(UINT_PTR id);
    void onLookupDone(int idx, struct LookupPayload* p);
    void scanDrive(int idx);
    void scanAllDrives();
    void startLookup(int idx, bool useCache);
    void decide(int idx);
    void verify(int idx);
    void openDialog(int idx, HWND owner);
    void notifyDialog();
    void shutdown();
    bool getPlaylist(int idx, IWMPPlaylist** pl, std::wstring* drive);
    void dumpAttributes(IWMPMedia* m, IWMPPlaylist* pl);
    bool writeYear(const std::vector<IWMPMedia*>& items, const std::string& year, std::string& dateValue,
                   int& written, int& unchanged);
    HWND findPlayerWindow();
    // Stall watchdog: a 250 ms timer on WMP's thread for a while after each disc change.
    // WM_TIMER only arrives when the thread is pumping messages, so a late tick is a freeze.
    void armWatchdog();
    void watchdogTick();

    LONG m_ref = 1;
    IWMPCore* m_core = nullptr;
    IConnectionPoint* m_cp = nullptr;
    DWORD m_cookie = 0;
    HWND m_hwnd = nullptr;       // hidden message window
    HWND m_hDlg = nullptr;       // open dialog, if any
    bool m_dumped = false;
    int m_lastDrive = -1;
    int m_yearMethod = -1;                 // which ReleaseDate spelling WMP accepted (probed once)
    std::map<int, DriveState> m_drives;
    bool m_watching = false;
    double m_watchUntil = 0, m_lastTick = 0, m_ourAtTick = 0;
};

} // namespace ln
