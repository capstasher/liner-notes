#include "cdtoc.h"
#include "common.h"
#include <winioctl.h>
#include <ntddcdrm.h>

namespace ln {

bool readDiscToc(const std::wstring& drive, DiscToc& out, std::string& error) {
    if (drive.size() < 2) { error = "bad drive specifier"; return false; }
    std::wstring dev = L"\\\\.\\" + drive.substr(0, 2);
    double t0 = nowMs();
    HANDLE h = CreateFileW(dev.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (nowMs() - t0 > 200) logf("Opening %s took %.0f ms.", narrow(dev).c_str(), nowMs() - t0);
    if (h == INVALID_HANDLE_VALUE) { error = "open " + narrow(dev) + " failed, error " + std::to_string(GetLastError()); return false; }

    CDROM_TOC toc = {};
    DWORD bytes = 0;
    BOOL ok = FALSE;
    for (int attempt = 0; attempt < 6 && !ok; ++attempt) {   // drive may still be spinning up
        ok = DeviceIoControl(h, IOCTL_CDROM_READ_TOC, nullptr, 0, &toc, sizeof toc, &bytes, nullptr);
        if (!ok) {
            DWORD e = GetLastError();
            if (e != ERROR_NOT_READY && e != ERROR_MEDIA_CHANGED && e != ERROR_BUSY) break;
            logf("Drive not ready to give its table of contents (error %lu); trying again in a second.", e);
            Sleep(1000);
        }
    }
    DWORD err = GetLastError();
    CloseHandle(h);
    if (!ok) { error = "IOCTL_CDROM_READ_TOC failed, error " + std::to_string(err); return false; }

    int first = toc.FirstTrack, last = toc.LastTrack;
    if (first < 1 || last < first || last > 99) { error = "implausible TOC track range"; return false; }
    DiscToc t;
    t.firstTrack = first;
    t.lastTrack = last;
    int n = last - first + 1;
    for (int i = 0; i <= n; ++i) {
        const TRACK_DATA& td = toc.TrackData[i];
        // IOCTL_CDROM_READ_TOC reports MSF addresses; MSF already includes the 2 s (150 frame) pregap.
        int frames = (td.Address[1] * 60 + td.Address[2]) * 75 + td.Address[3];
        if (i == n) { t.leadout = frames; break; }
        t.offsets.push_back(frames);
        t.isData.push_back((td.Control & 0x4) != 0);
    }
    if (!t.valid()) { error = "TOC failed validation: " + describeToc(t); return false; }
    out = t;
    return true;
}

} // namespace ln
