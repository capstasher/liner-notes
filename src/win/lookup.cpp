#include "lookup.h"
#include "cdtoc.h"
#include "common.h"
#include "http.h"
#include "wmpsdk.h"
#include "../core/json.h"
#include "../core/sha1.h"
#include "../core/sources.h"

namespace ln {

std::string userAgent() {
    // MusicBrainz asks for "Application/version ( contact )".
    std::string contact = settings().contactEmail.empty() ? "wmp-plugin" : settings().contactEmail;
    return std::string("LinerNotes-WMP/") + LINERNOTES_VERSION + " ( " + contact + " )";
}

// ---- MusicBrainz: max ~1 request/second per client, honour 503 + Retry-After ----
static CRITICAL_SECTION* mbLock() {
    static CRITICAL_SECTION cs;
    static bool init = [] { InitializeCriticalSection(&cs); return true; }();
    (void)init;
    return &cs;
}
static ULONGLONG g_lastMb = 0;

static HttpResponse mbGet(const std::string& pathAndQuery) {
    const bool verbose = settings().verboseLog;
    std::string url = "https://musicbrainz.org/ws/2/" + pathAndQuery;
    HttpResponse r;
    for (int attempt = 0; attempt < 4; ++attempt) {
        EnterCriticalSection(mbLock());
        ULONGLONG now = GetTickCount64();
        if (g_lastMb && now - g_lastMb < 1100) Sleep(DWORD(1100 - (now - g_lastMb)));
        const double t0 = nowMs();
        r = httpGet(url, userAgent());
        const double took = nowMs() - t0;
        g_lastMb = GetTickCount64();
        LeaveCriticalSection(mbLock());
        if (verbose) logf("MB GET %s -> %d (%u bytes, %.0f ms)%s%s", url.c_str(), r.status, unsigned(r.body.size()), took,
                                        r.error.empty() ? "" : " ", r.error.c_str());
        if (r.status != 503) break;
        Sleep(DWORD((r.retryAfterSec > 0 ? r.retryAfterSec : 2) * 1000));
    }
    return r;
}

static const char* kReleaseInc = "artist-credits+recordings+labels+release-groups";

// ---- the drive's TOC, remembered under WMP's own TOC string ----
// WMP reads every disc's TOC itself, but leaves data tracks out of the string it shows us, and
// MusicBrainz disc IDs need them. So the real TOC is read from the drive once, then remembered
// under WMP's string: a disc she's met before never needs the drive opening again, which can
// take seconds while the drive is busy.
static std::wstring tocAliasFile(const std::string& wmpToc) {
    std::string h = sha1(wmpToc), hex;
    static const char* digits = "0123456789abcdef";
    for (size_t i = 0; i < 10 && i < h.size(); ++i) { hex += digits[(unsigned char)h[i] >> 4]; hex += digits[(unsigned char)h[i] & 15]; }
    return cacheDir() + L"toc-" + widen(hex) + L".txt";
}

static bool loadTocAlias(const std::string& wmpToc, DiscToc& out) {
    if (wmpToc.empty()) return false;
    HANDLE h = CreateFileW(tocAliasFile(wmpToc).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[2048]; DWORD rd = 0;
    BOOL ok = ReadFile(h, buf, sizeof buf - 1, &rd, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    buf[rd] = 0;
    return tocFromString(buf, out);
}

static void saveTocAlias(const std::string& wmpToc, const DiscToc& toc) {
    if (wmpToc.empty()) return;
    std::string text = tocToString(toc) + "\r\n";
    HANDLE h = CreateFileW(tocAliasFile(wmpToc).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(h, text.data(), DWORD(text.size()), &w, nullptr);
    CloseHandle(h);
}

void lookupDisc(DiscLookup& d, bool useCache) {
    struct BackToIdle { ~BackToIdle() { setPhase(Phase::Idle); } } backToIdle;
    if (!d.tocOk && loadTocAlias(d.wmpToc, d.toc)) {
        d.tocOk = true;
        logLine("I know this disc's table of contents already, so I'm not reading the drive.");
    }
    if (!d.tocOk) {
        std::string err;
        setPhase(Phase::ReadingToc);
        const double t0 = nowMs();
        d.tocOk = readDiscToc(d.drive, d.toc, err);
        logf("Reading the table of contents took %.0f ms.", nowMs() - t0);
        setPhase(Phase::Idle);
        if (d.tocOk) saveTocAlias(d.wmpToc, d.toc);
        if (!d.tocOk) {
            d.status = "Could not read the disc's table of contents (" + err + ").";
            d.outcome = LookupOutcome::TocFailed;
            logLine(d.status);
            return;
        }
    }
    d.mbDiscId = musicBrainzDiscId(d.toc);
    d.freedbId = freedbDiscIdHex(d.toc);
    const int audioTracks = d.toc.audioTrackCount(), mbTracks = d.toc.mbTrackCount();
    logf("Disc in %s: %s | MB %s | freedb %s", narrow(d.drive).c_str(), describeToc(d.toc).c_str(), d.mbDiscId.c_str(), d.freedbId.c_str());
    auto align = [&](AlbumMeta& a) {
        if (int dropped = alignToDisc(a, d.toc))
            logf("Left %d data track(s) out of %s's tracklist for \"%s\"; WMP only lists the %d audio tracks.",
                 dropped, a.source.c_str(), a.title.c_str(), audioTracks);
    };

    if (useCache) {
        AlbumMeta cached;
        if (loadFromCache(d.mbDiscId, cached)) {
            align(cached);   // 1.0.0 saved mixed-mode discs with their data track in
            d.candidates.push_back(cached);
            d.fromCache = true;
            d.status = "Using saved details for this disc.";
            d.outcome = LookupOutcome::Cached;
            logLine("Cache hit.");
            return;
        }
    }

    // 1) MusicBrainz disc ID, with TOC fuzzy fallback and CD stubs.
    setPhase(Phase::AskingMusicBrainz);
    std::string q = "discid/" + d.mbDiscId + "?toc=" + musicBrainzTocParam(d.toc) + "&inc=" + kReleaseInc + "+genres&fmt=json";
    HttpResponse r = mbGet(q);
    if (r.status == 400) r = mbGet("discid/" + d.mbDiscId + "?toc=" + musicBrainzTocParam(d.toc) + "&inc=" + kReleaseInc + "&fmt=json");
    std::string mbProblem;
    if (r.status == 200) {
        json::Value root; std::string perr;
        if (json::parse(r.body, root, &perr)) parseMbDiscResponse(root, d.mbDiscId, mbTracks, d.candidates, audioTracks);
        else mbProblem = "MusicBrainz sent unreadable JSON (" + perr + ")";
    } else if (r.status == 404) {
        logLine("MusicBrainz: disc not found.");
    } else {
        mbProblem = r.status ? "MusicBrainz returned HTTP " + std::to_string(r.status) : "MusicBrainz unreachable: " + r.error;
        logLine(mbProblem);
    }

    // 2) GnuDB, only if nothing yet and the user has supplied the contact address GnuDB requires.
    const Settings s = settings();
    setPhase(Phase::Idle);
    if (d.candidates.empty() && s.useGnuDb && !s.contactEmail.empty()) {
        setPhase(Phase::AskingGnuDb);
        std::string user = s.contactEmail, host = "localhost";
        size_t at = user.find('@');
        if (at != std::string::npos) { host = user.substr(at + 1); user = user.substr(0, at); }
        std::string hello = "&hello=" + urlEncode(user) + "+" + urlEncode(host) + "+LinerNotes-WMP-plugin+" + LINERNOTES_VERSION + "&proto=6";
        std::string base = "https://gnudb.gnudb.org/~cddb/cddb.cgi?cmd=";
        HttpResponse g = httpGet(base + "cddb+query+" + gnudbQueryArgs(d.toc) + hello, userAgent());
        if (s.verboseLog) logf("GnuDB query -> %d: %.200s", g.status, g.body.c_str());
        std::vector<GnuMatch> matches;
        if (g.status == 200 && parseGnuQuery(g.body, matches)) {
            for (size_t i = 0; i < matches.size() && i < 5; ++i) {
                HttpResponse rd = httpGet(base + "cddb+read+" + matches[i].category + "+" + matches[i].discId + hello, userAgent());
                AlbumMeta a;
                if (rd.status != 200 || !parseGnuRead(rd.body, a)) continue;
                align(a);   // GnuDB lists data tracks too, so mixed-mode and Enhanced CDs need trimming
                if (int(a.tracks.size()) != audioTracks) continue;
                a.kind = matches[i].exact ? MatchKind::GnuDbExact : MatchKind::GnuDbInexact;
                a.id = matches[i].category + "/" + matches[i].discId;
                d.candidates.push_back(a);
            }
        }
    }

    setPhase(Phase::Idle);
    // Line every tracklist up with WMP's audio-only playlist; also fills missing lengths
    // (CD stubs and GnuDB often lack them) from the TOC.
    for (auto& a : d.candidates) align(a);

    rankCandidates(d.candidates, userCountryCode());
    d.outcome = !d.candidates.empty() ? LookupOutcome::Found
              : mbProblem.empty() ? LookupOutcome::NotFound : LookupOutcome::NetProblem;
    if (d.candidates.empty())
        d.status = mbProblem.empty() ? "No match found online for this disc." : mbProblem + ".";
    else if (d.candidates.size() == 1)
        d.status = "Found 1 match (" + std::string(matchKindName(d.candidates[0].kind)) + ").";
    else
        d.status = "Found " + std::to_string(d.candidates.size()) + " possible matches.";
    logLine(d.status);
}

bool searchReleases(const std::string& artist, const std::string& album, int trackCount, const DiscToc* toc,
                    std::vector<AlbumMeta>& out, std::string& error) {
    int alt = toc ? toc->mbTrackCount() : 0;   // a mixed-mode disc's releases count its data track
    std::string q = mbSearchQuery(artist, album, trackCount, alt);
    if (q.empty()) { error = "Enter an artist and/or album."; return false; }
    HttpResponse r = mbGet("release/?query=" + urlEncode(q) + "&limit=25&fmt=json");
    if (r.status != 200) { error = r.status ? "MusicBrainz returned HTTP " + std::to_string(r.status) : r.error; return false; }
    json::Value root; std::string perr;
    if (!json::parse(r.body, root, &perr)) { error = "Unreadable response: " + perr; return false; }
    parseMbSearchResponse(root, trackCount, out, alt);
    rankCandidates(out, userCountryCode());
    return true;
}

bool fetchRelease(const std::string& mbid, int trackCount, const DiscToc* toc, AlbumMeta& out, std::string& error) {
    HttpResponse r = mbGet("release/" + mbid + "?inc=" + kReleaseInc + "+genres&fmt=json");
    if (r.status == 400) r = mbGet("release/" + mbid + "?inc=" + kReleaseInc + "&fmt=json");
    if (r.status != 200) { error = r.status ? "MusicBrainz returned HTTP " + std::to_string(r.status) : r.error; return false; }
    json::Value root; std::string perr;
    if (!json::parse(r.body, root, &perr)) { error = "Unreadable response: " + perr; return false; }
    if (!parseMbRelease(root, "", trackCount, MatchKind::Search, out, toc ? toc->mbTrackCount() : 0)) {
        error = "That release has no medium with " + std::to_string(trackCount) + " tracks.";
        return false;
    }
    if (toc) alignToDisc(out, *toc);
    else dropDataTrackTitles(out, trackCount);
    return true;
}

// ---- cache: one JSON file per MusicBrainz disc ID ----
static std::wstring cacheFile(const std::string& id) { return cacheDir() + widen(id) + L".json"; }

void saveToCache(const std::string& id, const AlbumMeta& a) {
    if (id.empty()) return;
    std::string text = albumToJson(a);
    HANDLE h = CreateFileW(cacheFile(id).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(h, text.data(), DWORD(text.size()), &w, nullptr);
    CloseHandle(h);
}

bool loadFromCache(const std::string& id, AlbumMeta& out) {
    HANDLE h = CreateFileW(cacheFile(id).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string text;
    char buf[8192]; DWORD rd = 0;
    while (ReadFile(h, buf, sizeof buf, &rd, nullptr) && rd) text.append(buf, rd);
    CloseHandle(h);
    return albumFromJson(text, out);
}

void forgetCache(const std::string& id) { DeleteFileW(cacheFile(id).c_str()); }

} // namespace ln
