// The LinerNotes window: WMP's old "Find album info", hosted by Nōto.
//
// Modal, on the WMP thread; controls are created in code. Nōto sits top-left and
// speaks through the bubble beside her; her face follows the mood of her line
// (see src/core/voice.h). Everything below the bubble is the same window as 0.2.
#include "dialog.h"
#include "common.h"
#include "lookup.h"
#include "plugin.h"
#include "resource.h"
#include "../core/voice.h"
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ln {

namespace {
enum {
    IDC_MASCOT = 1001, IDC_BUBBLE, IDC_DISCINFO, IDC_CANDS, IDC_TRACKS, IDC_ARTIST, IDC_ALBUM, IDC_SEARCH,
    IDC_AUTOAPPLY, IDC_PROMPT, IDC_OVERWRITE, IDC_EMAIL, IDC_OPENLOG, IDC_RELOOKUP, IDC_APPLY,
    IDC_LBL_MATCHES, IDC_LBL_TRACKS, IDC_LBL_ARTIST, IDC_LBL_ALBUM, IDC_LBL_EMAIL, IDC_NOTE
};
const UINT WM_APP_SEARCH_DONE = WM_APP + 21;
const UINT WM_APP_FETCH_DONE = WM_APP + 22;
const UINT_PTR TIMER_CHATTER = 1;
const int kMascot = 84;          // logical px; the bitmaps are 168 px, i.e. sharp up to 200 % scaling
const int kShift = 44;           // the rest of the window sits this much lower than in 0.2

struct DlgCtx {
    Plugin* plugin = nullptr;
    int idx = 0;
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    HFONT bubbleFont = nullptr; // her lines, a little larger than the controls
    int dpi = 96;
    std::vector<AlbumMeta> list;
    bool listIsSearch = false;
    int token = 0;             // bumps whenever `list` is replaced, so late fetches are dropped
    bool searching = false;
    Line note;                 // overrides the drive's line (search results, clicks on Nōto, ...)
    bool noteIsChatter = false;
    unsigned chatter = 0;
    Line shown;                // what the bubble currently says
    HBITMAP sprite[4] = {};    // per Mood, pre-composited on the dialog colour
    HICON icon = nullptr;
};

struct SearchJob {
    HWND hwnd = nullptr; int token = 0; std::string artist, album; int tracks = 0;
    DiscToc toc; bool haveToc = false;
    std::vector<AlbumMeta> out; std::string error; bool ok = false;
};
struct FetchJob {
    HWND hwnd = nullptr; int token = 0; int row = 0; std::string mbid; int tracks = 0;
    DiscToc toc; bool haveToc = false;
    AlbumMeta out; std::string error; bool ok = false;
};

void searchWorker(void* a) {
    auto* j = static_cast<SearchJob*>(a);
    j->ok = searchReleases(j->artist, j->album, j->tracks, j->haveToc ? &j->toc : nullptr, j->out, j->error);
    if (!PostMessageW(j->hwnd, WM_APP_SEARCH_DONE, 0, LPARAM(j))) delete j;
}
void fetchWorker(void* a) {
    auto* j = static_cast<FetchJob*>(a);
    j->ok = fetchRelease(j->mbid, j->tracks, j->haveToc ? &j->toc : nullptr, j->out, j->error);
    if (!PostMessageW(j->hwnd, WM_APP_FETCH_DONE, 0, LPARAM(j))) delete j;
}

int S(const DlgCtx* c, int v) { return MulDiv(v, c->dpi, 96); }

// ---------------------------------------------------------------- Nōto's sprite

// Loads a 32-bit straight-alpha BMP resource, scales it to `target` px with an
// area-averaging filter (in premultiplied space, so edges stay clean), and composites
// it over `bg`. GDI only: no WIC, no GDI+, no AlphaBlend.
HBITMAP makeSprite(int resId, int target, COLORREF bg) {
    HBITMAP src = static_cast<HBITMAP>(LoadImageW(g_hInst, MAKEINTRESOURCEW(resId), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    if (!src) return nullptr;
    DIBSECTION ds = {};
    if (GetObjectW(src, sizeof ds, &ds) != sizeof ds || ds.dsBm.bmBitsPixel != 32 || !ds.dsBm.bmBits) {
        DeleteObject(src);
        return nullptr;
    }
    const int w = ds.dsBm.bmWidth, h = ds.dsBm.bmHeight, stride = ds.dsBm.bmWidthBytes;
    const bool bottomUp = ds.dsBmih.biHeight > 0;
    const BYTE* bits = static_cast<const BYTE*>(ds.dsBm.bmBits);
    std::vector<float> pm(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const BYTE* row = bits + size_t(bottomUp ? h - 1 - y : y) * stride;
        for (int x = 0; x < w; ++x) {
            const BYTE* p = row + x * 4;
            float a = p[3] / 255.0f;
            float* q = &pm[(size_t(y) * w + x) * 4];
            q[0] = p[0] * a; q[1] = p[1] * a; q[2] = p[2] * a; q[3] = p[3];
        }
    }
    DeleteObject(src);

    const int T = std::max(1, std::min({target, w, h}));
    // One axis at a time: each destination cell averages the source span it covers.
    auto resample = [](const std::vector<float>& in, int inW, int inH, int outW, int outH, bool horizontal) {
        std::vector<float> out(size_t(outW) * outH * 4, 0.0f);
        const int n = horizontal ? outW : outH, srcN = horizontal ? inW : inH, other = horizontal ? outH : outW;
        const double scale = double(srcN) / n;
        for (int d = 0; d < n; ++d) {
            double s0 = d * scale, s1 = (d + 1) * scale;
            for (int s = int(std::floor(s0)); s < int(std::ceil(s1)) && s < srcN; ++s) {
                float wgt = float((std::min(double(s + 1), s1) - std::max(double(s), s0)) / scale);
                if (wgt <= 0) continue;
                for (int o = 0; o < other; ++o) {
                    const float* a = horizontal ? &in[(size_t(o) * inW + s) * 4] : &in[(size_t(s) * inW + o) * 4];
                    float* b = horizontal ? &out[(size_t(o) * outW + d) * 4] : &out[(size_t(d) * outW + o) * 4];
                    for (int k = 0; k < 4; ++k) b[k] += a[k] * wgt;
                }
            }
        }
        return out;
    };
    std::vector<float> mid = resample(pm, w, h, T, h, true);
    std::vector<float> fin = resample(mid, T, h, T, T, false);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = T;
    bi.bmiHeader.biHeight = -T;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* outBits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &outBits, nullptr, 0);
    if (!dib || !outBits) { if (dib) DeleteObject(dib); return nullptr; }
    const float bgc[3] = {float(GetBValue(bg)), float(GetGValue(bg)), float(GetRValue(bg))};
    auto* o = static_cast<BYTE*>(outBits);
    for (size_t i = 0; i < size_t(T) * T; ++i) {
        float inv = 1.0f - std::min(255.0f, fin[i * 4 + 3]) / 255.0f;
        for (int k = 0; k < 3; ++k) o[i * 4 + k] = BYTE(std::min(255.0f, fin[i * 4 + k] + bgc[k] * inv + 0.5f));
        o[i * 4 + 3] = 255;
    }
    return dib;
}

void loadSprites(DlgCtx* c) {
    for (auto& b : c->sprite) if (b) { DeleteObject(b); b = nullptr; }
    const int ids[4] = {IDB_NOTO_IDLE, IDB_NOTO_BUSY, IDB_NOTO_HAPPY, IDB_NOTO_SORRY};
    for (int i = 0; i < 4; ++i) c->sprite[i] = makeSprite(ids[i], S(c, kMascot), GetSysColor(COLOR_BTNFACE));
}

void drawMascot(DlgCtx* c, const DRAWITEMSTRUCT* d) {
    FillRect(d->hDC, &d->rcItem, GetSysColorBrush(COLOR_BTNFACE));
    HBITMAP b = c->sprite[int(c->shown.mood)];
    if (!b) b = c->sprite[0];
    if (!b) return;
    BITMAP bm = {};
    GetObjectW(b, sizeof bm, &bm);
    HDC mem = CreateCompatibleDC(d->hDC);
    HGDIOBJ old = SelectObject(mem, b);
    const RECT& r = d->rcItem;
    BitBlt(d->hDC, r.left + (r.right - r.left - bm.bmWidth) / 2, r.top + (r.bottom - r.top - bm.bmHeight) / 2,
           bm.bmWidth, bm.bmHeight, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

// A rounded speech bubble whose tail points back at Nōto.
void drawBubble(DlgCtx* c, const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
    const int tail = S(c, 12), pw = std::max(1, S(c, 1)), rad = S(c, 16);
    RECT b = {r.left + tail, r.top + pw, r.right - pw, r.bottom - pw};
    const int ty = r.top + (r.bottom - r.top) * 64 / 100;

    HPEN pen = CreatePen(PS_SOLID, pw, GetSysColor(COLOR_BTNSHADOW));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetSysColorBrush(COLOR_WINDOW));
    POINT tri[3] = {{b.left + S(c, 4), ty - S(c, 7)}, {r.left + pw, ty + S(c, 4)}, {b.left + S(c, 4), ty + S(c, 5)}};
    Polygon(dc, tri, 3);
    RoundRect(dc, b.left, b.top, b.right, b.bottom, rad, rad);
    // Open the border where the tail joins, so bubble and tail read as one shape.
    HPEN seam = CreatePen(PS_SOLID, pw + 1, GetSysColor(COLOR_WINDOW));
    SelectObject(dc, seam);
    MoveToEx(dc, b.left + pw / 2, ty - S(c, 7) + pw + 1, nullptr);
    LineTo(dc, b.left + pw / 2, ty + S(c, 5) - pw);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(seam);
    DeleteObject(pen);

    std::wstring text = widen(c->shown.text);
    RECT t = {b.left + S(c, 12), b.top + S(c, 6), b.right - S(c, 12), b.bottom - S(c, 6)};
    HGDIOBJ oldFont = SelectObject(dc, c->bubbleFont ? c->bubbleFont : c->font);
    RECT calc = t;
    DrawTextW(dc, text.c_str(), -1, &calc, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    int ht = calc.bottom - calc.top;
    if (ht < t.bottom - t.top) t.top += (t.bottom - t.top - ht) / 2;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    DrawTextW(dc, text.c_str(), -1, &t, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont);
}

// ---------------------------------------------------------------- the usual window

HWND add(DlgCtx* c, const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h, DWORD ex = 0) {
    HWND hw = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(c, x), S(c, y), S(c, w), S(c, h),
                              c->hwnd, reinterpret_cast<HMENU>(INT_PTR(id)), g_hInst, nullptr);
    SendMessageW(hw, WM_SETFONT, WPARAM(c->font), TRUE);
    return hw;
}

void addColumn(HWND lv, int i, const wchar_t* text, int width) {
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(text);
    col.cx = width;
    ListView_InsertColumn(lv, i, &col);
}

void setCell(HWND lv, int row, int col, const std::string& text) {
    std::wstring w = widen(text);
    if (col == 0) {
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = row;
        it.pszText = const_cast<wchar_t*>(w.c_str());
        ListView_InsertItem(lv, &it);
    } else {
        ListView_SetItemText(lv, row, col, const_cast<wchar_t*>(w.c_str()));
    }
}

std::string getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring w(size_t(n) + 1, L'\0');
    GetWindowTextW(h, &w[0], n + 1);
    w.resize(size_t(n));
    return narrow(w);
}

int selectedRow(DlgCtx* c) { return ListView_GetNextItem(GetDlgItem(c->hwnd, IDC_CANDS), -1, LVNI_SELECTED); }

int trackCountForDisc(DlgCtx* c) {
    DriveState* st = c->plugin->driveState(c->idx);
    if (st && st->haveLookup && st->lookup.tocOk) return st->lookup.toc.audioTrackCount();
    return c->plugin->playlistTrackCount(c->idx);
}

bool tocForDisc(DlgCtx* c, DiscToc& out) {
    DriveState* st = c->plugin->driveState(c->idx);
    if (!st || !st->haveLookup || !st->lookup.tocOk) return false;
    out = st->lookup.toc;
    return true;
}

void setNote(DlgCtx* c, Line l, bool chatter = false) {
    c->note = std::move(l);
    c->noteIsChatter = chatter;
    if (!chatter) KillTimer(c->hwnd, TIMER_CHATTER);
}

void updateStatus(DlgCtx* c) {
    DriveState* st = c->plugin->driveState(c->idx);
    std::string info;
    if (st && !st->drive.empty()) {
        info = "Drive " + narrow(st->drive);
        if (st->haveLookup && st->lookup.tocOk)
            info += "   \xC2\xB7   " + std::to_string(st->lookup.toc.audioTrackCount()) + " tracks   \xC2\xB7   MusicBrainz disc ID " +
                    st->lookup.mbDiscId + "   \xC2\xB7   freedb " + st->lookup.freedbId;
    }
    SetDlgItemTextW(c->hwnd, IDC_DISCINFO, widen(info).c_str());

    Line line = (st && !st->say.empty()) ? st->say : noto::greeting();
    if (c->listIsSearch && c->searching) line = noto::searching();
    if (!c->note.empty()) line = c->note;
    c->shown = line;
    InvalidateRect(GetDlgItem(c->hwnd, IDC_BUBBLE), nullptr, TRUE);
    InvalidateRect(GetDlgItem(c->hwnd, IDC_MASCOT), nullptr, TRUE);
}

void showTracks(DlgCtx* c) {
    HWND lv = GetDlgItem(c->hwnd, IDC_TRACKS);
    ListView_DeleteAllItems(lv);
    int row = selectedRow(c);
    if (row < 0 || row >= int(c->list.size())) return;
    const AlbumMeta& a = c->list[size_t(row)];
    if (!a.tracksLoaded) { setCell(lv, 0, 0, ""); setCell(lv, 0, 1, "Fetching the tracklist\xE2\x80\xA6"); return; }
    for (size_t i = 0; i < a.tracks.size(); ++i) {
        int r = int(i);
        setCell(lv, r, 0, std::to_string(a.tracks[i].number));
        setCell(lv, r, 1, a.tracks[i].title);
        setCell(lv, r, 2, a.trackArtist(i));
        setCell(lv, r, 3, formatDuration(a.tracks[i].lengthMs));
    }
}

void fillCandidates(DlgCtx* c, int selectRow) {
    HWND lv = GetDlgItem(c->hwnd, IDC_CANDS);
    ListView_DeleteAllItems(lv);
    for (size_t i = 0; i < c->list.size(); ++i) {
        const AlbumMeta& a = c->list[i];
        int r = int(i);
        std::string title = a.displayTitle();
        if (!a.disambiguation.empty()) title += " (" + a.disambiguation + ")";
        setCell(lv, r, 0, title);
        setCell(lv, r, 1, a.artist);
        setCell(lv, r, 2, a.year());
        setCell(lv, r, 3, a.country);
        setCell(lv, r, 4, a.label + (a.catalogNumber.empty() ? "" : " [" + a.catalogNumber + "]"));
        setCell(lv, r, 5, a.source + " / " + matchKindName(a.kind));
    }
    if (!c->list.empty()) {
        int s = (selectRow >= 0 && selectRow < int(c->list.size())) ? selectRow : 0;
        ListView_SetItemState(lv, s, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(lv, s, FALSE);
    }
    showTracks(c);
    EnableWindow(GetDlgItem(c->hwnd, IDC_APPLY), !c->list.empty());
}

void loadFromPlugin(DlgCtx* c) {
    if (c->listIsSearch) { updateStatus(c); return; }
    DriveState* st = c->plugin->driveState(c->idx);
    c->list = (st && st->haveLookup) ? st->lookup.candidates : std::vector<AlbumMeta>();
    c->token++;
    int sel = 0;
    if (st && st->hasApplied)
        for (size_t i = 0; i < c->list.size(); ++i)
            if (c->list[i].id == st->applied.id) { sel = int(i); break; }
    fillCandidates(c, sel);
    // Pre-fill the search boxes with the best guess so a manual search is one click.
    if (!c->list.empty() && GetWindowTextLengthW(GetDlgItem(c->hwnd, IDC_ALBUM)) == 0) {
        SetDlgItemTextW(c->hwnd, IDC_ARTIST, widen(c->list[0].artist).c_str());
        SetDlgItemTextW(c->hwnd, IDC_ALBUM, widen(c->list[0].title).c_str());
    }
    updateStatus(c);
}

void ensureTracks(DlgCtx* c, int row) {
    if (row < 0 || row >= int(c->list.size()) || c->list[size_t(row)].tracksLoaded) return;
    auto* j = new FetchJob();
    j->hwnd = c->hwnd; j->token = c->token; j->row = row; j->mbid = c->list[size_t(row)].id;
    j->tracks = trackCountForDisc(c);
    j->haveToc = tocForDisc(c, j->toc);
    if (!startWorker(fetchWorker, j)) delete j;
}

void saveSettingsFromUi(DlgCtx* c) {
    Settings s = settings();
    s.autoApply = IsDlgButtonChecked(c->hwnd, IDC_AUTOAPPLY) == BST_CHECKED;
    s.promptWhenUnsure = IsDlgButtonChecked(c->hwnd, IDC_PROMPT) == BST_CHECKED;
    s.overwriteExisting = IsDlgButtonChecked(c->hwnd, IDC_OVERWRITE) == BST_CHECKED;
    s.contactEmail = getText(GetDlgItem(c->hwnd, IDC_EMAIL));
    while (!s.contactEmail.empty() && s.contactEmail.back() == ' ') s.contactEmail.pop_back();
    updateSettings(s);
}

void onApply(DlgCtx* c) {
    int row = selectedRow(c);
    if (row < 0 || row >= int(c->list.size())) return;
    const AlbumMeta& a = c->list[size_t(row)];
    if (!a.tracksLoaded) { setNote(c, noto::stillLoading()); updateStatus(c); return; }
    int wmpTracks = c->plugin->playlistTrackCount(c->idx);
    if (wmpTracks > 0 && int(a.tracks.size()) != wmpTracks) {
        std::wstring q = widen(noto::trackMismatch(int(a.tracks.size()), wmpTracks));
        if (MessageBoxW(c->hwnd, q.c_str(), widen(noto::name()).c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    std::string msg;
    c->plugin->applyAlbum(c->idx, a, true, msg);
    DriveState* st = c->plugin->driveState(c->idx);
    setNote(c, st ? st->say : Line());
    updateStatus(c);
}

void onSearch(DlgCtx* c) {
    if (c->searching) return;
    auto* j = new SearchJob();
    j->hwnd = c->hwnd;
    j->artist = getText(GetDlgItem(c->hwnd, IDC_ARTIST));
    j->album = getText(GetDlgItem(c->hwnd, IDC_ALBUM));
    j->tracks = trackCountForDisc(c);
    j->haveToc = tocForDisc(c, j->toc);
    if (j->artist.empty() && j->album.empty()) { delete j; setNote(c, noto::needSearchTerms()); updateStatus(c); return; }
    c->searching = true;
    c->listIsSearch = true;
    setNote(c, Line());
    c->token++;
    j->token = c->token;
    c->list.clear();
    fillCandidates(c, 0);
    updateStatus(c);
    if (!startWorker(searchWorker, j)) { delete j; c->searching = false; setNote(c, noto::threadFailed()); updateStatus(c); }
}

void onMascotClicked(DlgCtx* c) {
    setNote(c, noto::chatter(c->chatter++), true);
    SetTimer(c->hwnd, TIMER_CHATTER, 7000, nullptr);   // then back to business
    updateStatus(c);
}

void layout(DlgCtx* c) {
    const int k = kShift;
    add(c, L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY, IDC_MASCOT, 8, 6, kMascot, kMascot);
    add(c, L"STATIC", L"", SS_OWNERDRAW, IDC_BUBBLE, 96, 10, 652, 56);
    add(c, L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, IDC_DISCINFO, 110, 70, 638, 16);

    add(c, L"STATIC", L"&What I found for this disc:", 0, IDC_LBL_MATCHES, 12, 52 + k, 300, 16);
    HWND cands = add(c, WC_LISTVIEWW, L"", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, IDC_CANDS, 12, 70 + k, 736, 150);
    ListView_SetExtendedListViewStyle(cands, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    addColumn(cands, 0, L"Album", S(c, 230)); addColumn(cands, 1, L"Artist", S(c, 150)); addColumn(cands, 2, L"Year", S(c, 48));
    addColumn(cands, 3, L"Country", S(c, 56)); addColumn(cands, 4, L"Label", S(c, 130)); addColumn(cands, 5, L"Source", S(c, 130));
    add(c, L"STATIC", L"&Tracklist:", 0, IDC_LBL_TRACKS, 12, 228 + k, 300, 16);
    HWND tracks = add(c, WC_LISTVIEWW, L"", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_NOSORTHEADER, IDC_TRACKS, 12, 246 + k, 736, 170);
    ListView_SetExtendedListViewStyle(tracks, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    addColumn(tracks, 0, L"#", S(c, 36)); addColumn(tracks, 1, L"Title", S(c, 360)); addColumn(tracks, 2, L"Artist", S(c, 240));
    addColumn(tracks, 3, L"Length", S(c, 64));

    add(c, L"STATIC", L"A&rtist:", 0, IDC_LBL_ARTIST, 12, 429 + k, 44, 18);
    add(c, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_ARTIST, 58, 426 + k, 220, 23, WS_EX_CLIENTEDGE);
    add(c, L"STATIC", L"Al&bum:", 0, IDC_LBL_ALBUM, 290, 429 + k, 44, 18);
    add(c, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_ALBUM, 336, 426 + k, 250, 23, WS_EX_CLIENTEDGE);
    add(c, L"BUTTON", L"&Search MusicBrainz", WS_TABSTOP | BS_PUSHBUTTON, IDC_SEARCH, 598, 425 + k, 150, 25);

    add(c, L"BUTTON", L"Fill in clear matches &automatically", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_AUTOAPPLY, 12, 462 + k, 250, 20);
    add(c, L"BUTTON", L"Ask &me when I'm not sure", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_PROMPT, 268, 462 + k, 220, 20);
    add(c, L"BUTTON", L"&Overwrite titles WMP already has", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_OVERWRITE, 494, 462 + k, 254, 20);
    add(c, L"STATIC", L"Contact &e-mail (MusicBrainz / GnuDB):", 0, IDC_LBL_EMAIL, 12, 494 + k, 228, 18);
    add(c, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IDC_EMAIL, 240, 491 + k, 250, 23, WS_EX_CLIENTEDGE);
    add(c, L"BUTTON", L"Open my &notebook (log)", WS_TABSTOP | BS_PUSHBUTTON, IDC_OPENLOG, 598, 490 + k, 150, 25);
    add(c, L"STATIC", L"GnuDB only talks to people who leave an e-mail. Their rule, not mine!", SS_NOPREFIX,
        IDC_NOTE, 240, 516 + k, 356, 16);

    add(c, L"BUTTON", L"Look a&gain", WS_TABSTOP | BS_PUSHBUTTON, IDC_RELOOKUP, 12, 548 + k, 130, 28);
    add(c, L"BUTTON", L"A&pply to CD", WS_TABSTOP | BS_DEFPUSHBUTTON, IDC_APPLY, 508, 548 + k, 120, 28);
    add(c, L"BUTTON", L"Close", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL, 638, 548 + k, 110, 28);

    const Settings s = settings();
    CheckDlgButton(c->hwnd, IDC_AUTOAPPLY, s.autoApply ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(c->hwnd, IDC_PROMPT, s.promptWhenUnsure ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(c->hwnd, IDC_OVERWRITE, s.overwriteExisting ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(c->hwnd, IDC_EMAIL, widen(s.contactEmail).c_str());
}

INT_PTR CALLBACK dlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<DlgCtx*>(GetWindowLongPtrW(h, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        c = reinterpret_cast<DlgCtx*>(lp);
        SetWindowLongPtrW(h, DWLP_USER, LONG_PTR(c));
        c->hwnd = h;
        HDC dc = GetDC(h); c->dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(h, dc);
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof ncm;
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        c->font = CreateFontIndirectW(&ncm.lfMessageFont);
        LOGFONTW big = ncm.lfMessageFont;
        big.lfHeight = MulDiv(big.lfHeight, 115, 100);
        c->bubbleFont = CreateFontIndirectW(&big);
        SetWindowTextW(h, widen("LinerNotes \xE2\x99\xAA " + noto::name() + "'s album finder").c_str());
        c->icon = static_cast<HICON>(LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_LINERNOTES), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
        if (c->icon) SendMessageW(h, WM_SETICON, ICON_SMALL, LPARAM(c->icon));
        RECT rc = {0, 0, S(c, 760), S(c, 588 + kShift)};
        AdjustWindowRectEx(&rc, DWORD(GetWindowLongPtrW(h, GWL_STYLE)), FALSE, DWORD(GetWindowLongPtrW(h, GWL_EXSTYLE)));
        int w = rc.right - rc.left, ht = rc.bottom - rc.top;
        RECT owner;
        HWND o = GetWindow(h, GW_OWNER);
        if (!o || !GetWindowRect(o, &owner)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &owner, 0);
        SetWindowPos(h, nullptr, owner.left + ((owner.right - owner.left) - w) / 2, owner.top + ((owner.bottom - owner.top) - ht) / 2,
                     w, ht, SWP_NOZORDER);
        loadSprites(c);
        layout(c);
        c->plugin->setDialog(h);
        loadFromPlugin(c);
        SendMessageW(h, DM_SETDEFID, IDC_APPLY, 0);
        SetFocus(GetDlgItem(h, IDC_CANDS));
        return FALSE;
    }
    case WM_APP_DIALOG_REFRESH:
        if (c) {
            if (c->noteIsChatter) setNote(c, Line());   // real news beats small talk
            loadFromPlugin(c);
        }
        return TRUE;
    case WM_APP_SEARCH_DONE: {
        auto* j = reinterpret_cast<SearchJob*>(lp);
        if (c && j->token == c->token) {
            c->searching = false;
            c->list = j->out;
            setNote(c, !j->ok ? noto::searchFailed()
                       : c->list.empty() ? noto::searchEmpty(j->tracks)
                       : noto::searchHits(int(c->list.size())));
            if (!j->ok) logf("Search failed: %s", j->error.c_str());
            fillCandidates(c, 0);
            ensureTracks(c, 0);
            updateStatus(c);
        }
        delete j;
        return TRUE;
    }
    case WM_APP_FETCH_DONE: {
        auto* j = reinterpret_cast<FetchJob*>(lp);
        if (c && j->token == c->token && j->row < int(c->list.size())) {
            if (j->ok) { c->list[size_t(j->row)] = j->out; }
            else { logf("Could not load tracklist: %s", j->error.c_str()); setNote(c, noto::tracklistFailed()); updateStatus(c); }
            if (selectedRow(c) == j->row) showTracks(c);
        }
        delete j;
        return TRUE;
    }
    case WM_TIMER:
        if (c && wp == TIMER_CHATTER) {
            KillTimer(h, TIMER_CHATTER);
            if (c->noteIsChatter) { setNote(c, Line()); updateStatus(c); }
        }
        return TRUE;
    case WM_DRAWITEM: {
        auto* d = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (!c) break;
        if (d->CtlID == IDC_MASCOT) { drawMascot(c, d); return TRUE; }
        if (d->CtlID == IDC_BUBBLE) { drawBubble(c, d); return TRUE; }
        break;
    }
    case WM_CTLCOLORSTATIC:
        if (c && (HWND(lp) == GetDlgItem(h, IDC_DISCINFO) || HWND(lp) == GetDlgItem(h, IDC_NOTE))) {
            SetTextColor(HDC(wp), GetSysColor(COLOR_GRAYTEXT));
            SetBkColor(HDC(wp), GetSysColor(COLOR_BTNFACE));
            return INT_PTR(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    case WM_SYSCOLORCHANGE:
        if (c) { loadSprites(c); InvalidateRect(h, nullptr, TRUE); }
        break;
    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->idFrom == IDC_CANDS) {
            if (hdr->code == LVN_ITEMCHANGED) {
                auto* nm = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((nm->uNewState & LVIS_SELECTED) && !(nm->uOldState & LVIS_SELECTED)) {
                    showTracks(c); ensureTracks(c, nm->iItem);
                }
            } else if (hdr->code == NM_DBLCLK) {
                onApply(c);
            }
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_MASCOT:
            if (HIWORD(wp) == STN_CLICKED || HIWORD(wp) == STN_DBLCLK) onMascotClicked(c);
            return TRUE;
        case IDC_APPLY: {
            HWND f = GetFocus();   // Enter in a search box means "search", not "apply"
            if (f == GetDlgItem(h, IDC_ARTIST) || f == GetDlgItem(h, IDC_ALBUM)) onSearch(c); else onApply(c);
            return TRUE;
        }
        case IDC_SEARCH: onSearch(c); return TRUE;
        case IDC_RELOOKUP:
            c->listIsSearch = false;
            setNote(c, Line());
            c->plugin->relookup(c->idx, false);
            loadFromPlugin(c);
            return TRUE;
        case IDC_OPENLOG:
            ShellExecuteW(h, L"open", dataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        case IDC_AUTOAPPLY: case IDC_PROMPT: case IDC_OVERWRITE:
            saveSettingsFromUi(c);
            return TRUE;
        case IDC_EMAIL:
            if (HIWORD(wp) == EN_KILLFOCUS) saveSettingsFromUi(c);
            return TRUE;
        case IDCANCEL:
            saveSettingsFromUi(c);
            EndDialog(h, 0);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        if (c) {
            KillTimer(h, TIMER_CHATTER);
            c->plugin->setDialog(nullptr);
            if (c->font) DeleteObject(c->font);
            if (c->bubbleFont) DeleteObject(c->bubbleFont);
            for (auto& b : c->sprite) if (b) { DeleteObject(b); b = nullptr; }
            if (c->icon) { DestroyIcon(c->icon); c->icon = nullptr; }
        }
        break;
    }
    return FALSE;
}
} // namespace

void runLinerNotesDialog(Plugin* plugin, HWND owner, int driveIdx) {
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    // Empty in-memory template; every control is created in WM_INITDIALOG.
    struct alignas(4) Tmpl { DLGTEMPLATE t; WORD menu, cls, title; } tmpl = {};
    tmpl.t.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_CLIPCHILDREN;
    tmpl.t.cx = 400; tmpl.t.cy = 300;
    DlgCtx ctx;
    ctx.plugin = plugin;
    ctx.idx = driveIdx;
    DialogBoxIndirectParamW(g_hInst, &tmpl.t, owner, dlgProc, LPARAM(&ctx));
}

} // namespace ln
