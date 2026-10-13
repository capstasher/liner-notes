#include "http.h"
#include "common.h"
#include <winhttp.h>
#include <vector>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

namespace ln {

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};

static std::string lastErr(const char* what) {
    return std::string(what) + " failed (Win32 error " + std::to_string(GetLastError()) + ")";
}

HttpResponse httpGet(const std::string& url, const std::string& userAgent, int timeoutMs) {
    HttpResponse r;
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = L"", path[4096] = L"";
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 4096;
    wchar_t extra[4096] = L"";
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { r.error = lastErr("WinHttpCrackUrl"); return r; }
    std::wstring object = std::wstring(path) + extra;

    Handle session;
    std::wstring ua = widen(userAgent);
    session.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) session.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) { r.error = lastErr("WinHttpOpen"); return r; }
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;   // pre-TLS1.3 builds of Windows
        WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
    WinHttpSetTimeouts(session.h, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    Handle conn;
    conn.h = WinHttpConnect(session.h, host, uc.nPort, 0);
    if (!conn.h) { r.error = lastErr("WinHttpConnect"); return r; }
    Handle req;
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    req.h = WinHttpOpenRequest(conn.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req.h) { r.error = lastErr("WinHttpOpenRequest"); return r; }
    const wchar_t* hdr = L"Accept: application/json, text/plain;q=0.9, */*;q=0.5\r\n";
    if (!WinHttpSendRequest(req.h, hdr, DWORD(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.h, nullptr)) {
        r.error = lastErr("HTTP request"); return r;
    }
    DWORD status = 0, sz = sizeof status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    r.status = int(status);
    DWORD ra = 0; sz = sizeof ra;
    if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_RETRY_AFTER | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &ra, &sz, WINHTTP_NO_HEADER_INDEX))
        r.retryAfterSec = int(ra);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail)) { r.error = lastErr("WinHttpQueryDataAvailable"); break; }
        if (avail == 0) break;
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(req.h, buf.data(), avail, &read)) { r.error = lastErr("WinHttpReadData"); break; }
        r.body.append(buf.data(), read);
        if (r.body.size() > 8 * 1024 * 1024) { r.error = "response too large"; break; }
    }
    return r;
}

} // namespace ln
