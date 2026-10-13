// COM server plumbing + self-registration as a WMP background UI plug-in.
#include "common.h"
#include "plugin.h"
#include "wmpsdk.h"

using namespace ln;

namespace {
class ClassFactory final : public IClassFactory {
    LONG m_ref = 1;
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ULONG(InterlockedIncrement(&m_ref)); }
    STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return ULONG(r); }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        Plugin* p = new (std::nothrow) Plugin();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_lockCount); else InterlockedDecrement(&g_lockCount);
        return S_OK;
    }
};

LONG setString(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    HKEY k;
    LONG rc = RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
    if (rc != ERROR_SUCCESS) return rc;
    rc = RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return rc;
}
LONG setDword(HKEY root, const std::wstring& key, const wchar_t* name, DWORD value) {
    HKEY k;
    LONG rc = RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
    if (rc != ERROR_SUCCESS) return rc;
    rc = RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof value);
    RegCloseKey(k);
    return rc;
}
const std::wstring kClsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + LINERNOTES_CLSID_STR;
const std::wstring kPluginKey = std::wstring(PLUGIN_INSTALLREGKEY) + L"\\" + LINERNOTES_CLSID_STR;

void notifyWmp() {
    // What wmpplug.h's inline WMPNotifyPluginAddRemove() does.
    PostMessageW(HWND_BROADCAST, RegisterWindowMessageA("WMPlayer_PluginAddRemove"), 0, 0);
}
} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_hInst = h; DisableThreadLibraryCalls(h); }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(clsid, CLSID_LinerNotesPlugin)) return CLASS_E_CLASSNOTAVAILABLE;
    ClassFactory* f = new (std::nothrow) ClassFactory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDAPI DllCanUnloadNow() { return g_lockCount == 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    LONG rc = setString(HKEY_LOCAL_MACHINE, kClsidKey, nullptr, L"LinerNotes WMP plug-in");
    if (rc == ERROR_SUCCESS) rc = setString(HKEY_LOCAL_MACHINE, kClsidKey + L"\\InprocServer32", nullptr, path);
    if (rc == ERROR_SUCCESS) rc = setString(HKEY_LOCAL_MACHINE, kClsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    // Shown in WMP's Plug-ins list. \u266A = musical note, \u014D = o-macron (N\u014Dto).
    if (rc == ERROR_SUCCESS) rc = setString(HKEY_LOCAL_MACHINE, kPluginKey, L"FriendlyName", L"LinerNotes \u266A CD album info");
    if (rc == ERROR_SUCCESS) rc = setString(HKEY_LOCAL_MACHINE, kPluginKey, L"Description",
        L"N\u014Dto reads your CD's table of contents and asks MusicBrainz (and GnuDB, if you let her) "
        L"for the album and track names WMP can no longer fetch.");
    if (rc == ERROR_SUCCESS) rc = setDword(HKEY_LOCAL_MACHINE, kPluginKey, L"Capabilities",
        PLUGIN_TYPE_BACKGROUND | PLUGIN_FLAGS_HASPROPERTYPAGE | PLUGIN_FLAGS_INSTALLAUTORUN);
    if (rc != ERROR_SUCCESS) return (rc == ERROR_ACCESS_DENIED) ? E_ACCESSDENIED : HRESULT_FROM_WIN32(rc);
    notifyWmp();
    return S_OK;
}

STDAPI DllUnregisterServer() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kPluginKey.c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kClsidKey.c_str());
    notifyWmp();
    return S_OK;
}
