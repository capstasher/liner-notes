// Pulls in the WMP object model and supplies the pieces of wmpplug.h we need
// (that header is not shipped with MinGW, and is optional in newer Windows SDKs).
// IID and vtable order verified against the WMP SDK's wmpplug.h.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <wmp.h>

#ifndef __IWMPPluginUI_INTERFACE_DEFINED__
#define __IWMPPluginUI_INTERFACE_DEFINED__
MIDL_INTERFACE("4C5E8F9F-AD3E-4bf9-9753-FCD30D6D38DD")
IWMPPluginUI : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SetCore(IWMPCore* pCore) = 0;
    virtual HRESULT STDMETHODCALLTYPE Create(HWND hwndParent, HWND* phwndWindow) = 0;
    virtual HRESULT STDMETHODCALLTYPE Destroy(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE DisplayPropertyPage(HWND hwndParent) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(const WCHAR* pwszName, VARIANT* pvarProperty) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProperty(const WCHAR* pwszName, const VARIANT* pvarProperty) = 0;
    virtual HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG lpmsg) = 0;
};
#endif
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IWMPPluginUI, 0x4C5E8F9F, 0xAD3E, 0x4bf9, 0x97, 0x53, 0xFC, 0xD3, 0x0D, 0x6D, 0x38, 0xDD)
#endif

#ifndef PLUGIN_TYPE_BACKGROUND
#define PLUGIN_INSTALLREGKEY              L"Software\\Microsoft\\MediaPlayer\\UIPlugins"
#define PLUGIN_TYPE_BACKGROUND            0x1
#define PLUGIN_FLAGS_HASPROPERTYPAGE      0x80000000
#define PLUGIN_FLAGS_INSTALLAUTORUN       0x40000000
#define PLUGIN_MISC_QUERYDESTROY          L"QueryDestroy"
#endif

// The MinGW (Wine-derived) wmp.h and Microsoft's wmp.h spell a few accessors differently.
#if defined(__MINGW32__)
#define WMP_CDCOLL_ITEM     Item
#define WMP_CDROM_PLAYLIST  get_Playlist
#define WMP_PLAYLIST_ITEM   get_Item
#else
#define WMP_CDCOLL_ITEM     item
#define WMP_CDROM_PLAYLIST  get_playlist
#define WMP_PLAYLIST_ITEM   get_item
#endif

// {6B1C43E2-5F0A-4C8B-9E5D-2A7F31C9D4B8}
static const CLSID CLSID_LinerNotesPlugin =
    {0x6b1c43e2, 0x5f0a, 0x4c8b, {0x9e, 0x5d, 0x2a, 0x7f, 0x31, 0xc9, 0xd4, 0xb8}};
#define LINERNOTES_CLSID_STR L"{6B1C43E2-5F0A-4C8B-9E5D-2A7F31C9D4B8}"
#define LINERNOTES_NAME      L"LinerNotes"
#define LINERNOTES_VERSION   "1.0.1"
