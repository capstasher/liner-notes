// Host-side unit tests for the portable core (build with any C++17 compiler).
#include "../src/core/album.h"
#include "../src/core/discid.h"
#include "../src/core/json.h"
#include "../src/core/sha1.h"
#include "../src/core/sources.h"
#include "../src/core/voice.h"
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { std::printf("FAIL %s:%d  %s == %s\n   got: [%s]\n  want: [%s]\n", __FILE__, __LINE__, #a, #b, std::string(_a).c_str(), std::string(_b).c_str()); ++failures; } } while (0)

static std::string hex(const std::string& raw) {
    std::string o; char b[3];
    for (unsigned char c : raw) { std::snprintf(b, 3, "%02x", c); o += b; }
    return o;
}

static ln::DiscToc makeToc(int first, std::vector<int> offs, int leadout, std::vector<bool> data = {}) {
    ln::DiscToc t;
    t.firstTrack = first;
    t.lastTrack = first + int(offs.size()) - 1;
    t.offsets = offs;
    t.isData = data.empty() ? std::vector<bool>(offs.size(), false) : data;
    t.leadout = leadout;
    return t;
}


// Decodes UTF-8; false on any malformed sequence. Collects code points.
static bool decodeUtf8(const std::string& s, std::vector<unsigned>& out) {
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + n >= s.size() + (n ? 0 : 1)) return false;
        unsigned cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k <= n; ++k) {
            unsigned char d = (unsigned char)s[i + k];
            if ((d >> 6) != 2) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        out.push_back(cp);
        i += size_t(n) + 1;
    }
    return true;
}

// Nōto must only use glyphs a stock Windows dialog font (Segoe UI plus its
// standard fallbacks) can draw: Latin, Greek, general punctuation, a few symbols
// and CJK punctuation / half- and full-width forms. No emoji, nothing astral.
static bool windowsSafe(unsigned cp) {
    if (cp < 0x0080) return cp >= 0x20 || cp == '\n';
    if (cp >= 0x00A0 && cp <= 0x017F) return true;     // Latin-1, Latin Extended-A (ō)
    if (cp >= 0x0370 && cp <= 0x03FF) return true;     // Greek (ω)
    if (cp >= 0x2010 && cp <= 0x2027) return true;     // dashes, quotes, ellipsis
    if (cp == 0x2266 || cp == 0x2267) return true;     // ≦ ≧
    if (cp == 0x25BD) return true;                     // ▽
    if (cp == 0x266A) return true;                     // ♪
    if (cp == 0x2727) return true;                     // ✧
    if (cp >= 0x3000 && cp <= 0x30FF) return true;     // CJK punctuation, kana (・)
    if (cp >= 0xFF01 && cp <= 0xFF5E) return true;     // full-width forms (；｀)
    return false;
}

static void checkLine(const ln::Line& l, const char* what) {
    std::vector<unsigned> cps;
    bool ok = decodeUtf8(l.text, cps);
    if (!ok) { std::printf("FAIL voice %s: invalid UTF-8\n", what); ++failures; return; }
    if (cps.empty()) { std::printf("FAIL voice %s: empty\n", what); ++failures; }
    if (cps.size() > 150) { std::printf("FAIL voice %s: %u chars, too long for the bubble\n", what, unsigned(cps.size())); ++failures; }
    for (unsigned cp : cps)
        if (!windowsSafe(cp)) { std::printf("FAIL voice %s: U+%04X may not render\n", what, cp); ++failures; break; }
}

int main() {
    // --- SHA-1 known answers
    CHECK_EQ(hex(ln::sha1("abc")), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
    CHECK_EQ(hex(ln::sha1("")), std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
    CHECK_EQ(hex(ln::sha1(std::string(1000, 'a'))), std::string("291e9a6c66994949b57ba5e650361e98fc36b1ba"));

    // --- MusicBrainz worked example (Disc ID Calculation page): 6 audio tracks
    auto t6 = makeToc(1, {150, 15363, 32314, 46592, 63414, 80489}, 95462);
    CHECK(t6.valid());
    CHECK_EQ(ln::musicBrainzDiscId(t6), std::string("49HHV7Eb8UKF3aQiNmu1GR8vKTY-"));
    CHECK_EQ(ln::freedbDiscIdHex(t6), std::string("3404f606"));
    CHECK_EQ(ln::musicBrainzTocParam(t6), std::string("1+6+95462+150+15363+32314+46592+63414+80489"));

    // --- Example TOC from the MusicBrainz API docs (fuzzy lookup example)
    auto t12 = makeToc(1, {150, 22767, 41887, 58317, 72102, 91375, 104652, 115380, 132165, 143932, 159870, 174597}, 267257);
    CHECK_EQ(ln::musicBrainzDiscId(t12), std::string("I5l9cCSFccLKFEKS.7wqSZAorPU-"));

    // --- Enhanced CD (trailing data track): MB drops it and pulls the lead-out back 11400 frames
    auto te = makeToc(1, {150, 14109, 33586, 53077, 65781, 77892, 99174, 125974}, 188483,
                      {false, false, false, false, false, false, false, true});
    CHECK(te.valid());
    CHECK(te.mbLastTrack() == 7);
    CHECK(te.mbLeadout() == 114574);   // 150 + 125824 - 11400, as in the MB docs
    CHECK(te.audioTrackCount() == 7);
    CHECK_EQ(ln::musicBrainzTocParam(te), std::string("1+7+114574+150+14109+33586+53077+65781+77892+99174"));

    // --- Remembered TOCs round-trip, data flags included
    {
        ln::DiscToc back;
        CHECK_EQ(ln::tocToString(te), std::string("1 8 188483 150 14109 33586 53077 65781 77892 99174 125974d"));
        CHECK(ln::tocFromString(ln::tocToString(te) + "\r\n", back));
        CHECK(back.offsets == te.offsets && back.isData == te.isData && back.leadout == te.leadout);
        CHECK_EQ(ln::musicBrainzDiscId(back), ln::musicBrainzDiscId(te));
        CHECK(!ln::tocFromString("", back));
        CHECK(!ln::tocFromString("1 2 300 150 x", back));
        CHECK(!ln::tocFromString("1 2 100 150 200", back));      // lead-out before the last track
    }

    // --- WM/MCDI fallback parser
    ln::DiscToc fromMcdi;
    CHECK(ln::parseWmpMcdi("6+96+3C03+7E3A+B600+F7B6+13A69+174E6", fromMcdi));
    CHECK_EQ(ln::musicBrainzDiscId(fromMcdi), std::string("49HHV7Eb8UKF3aQiNmu1GR8vKTY-"));
    CHECK(!ln::parseWmpMcdi("garbage", fromMcdi));

    // --- JSON parser
    ln::json::Value v;
    std::string err;
    CHECK(ln::json::parse(R"({"a":[1,2.5,-3e2],"s":"caf\u00e9 \ud83d\udcbf","t":true,"n":null})", v, &err));
    CHECK(v["a"].size() == 3);
    CHECK(v["a"][2].num() == -300.0);
    CHECK_EQ(v["s"].str(), std::string("caf\xC3\xA9 \xF0\x9F\x92\xBF"));
    CHECK(v["missing"]["deeper"].isNull());
    CHECK(!ln::json::parse("{\"a\":", v, &err));

    // --- MusicBrainz disc-ID response (shape of /ws/2/discid/<id>?inc=artist-credits+recordings+labels+release-groups)
    const char* mbDisc = R"({
      "id": "49HHV7Eb8UKF3aQiNmu1GR8vKTY-", "sectors": 95462, "offset-count": 6,
      "releases": [
        { "id": "rel-us", "title": "Test Album", "status": "Official", "date": "1999-03-02", "country": "US",
          "barcode": "123", "disambiguation": "",
          "artist-credit": [{"name": "Alpha", "joinphrase": " & "}, {"name": "Beta", "joinphrase": ""}],
          "label-info": [{"catalog-number": "CAT-1", "label": {"name": "Label One"}}],
          "release-group": {"id": "0b1e4c2a-1111-4222-8333-944455556666", "first-release-date": "1998",
                            "genres": [{"name": "rock", "count": 3}]},
          "media": [
            {"position": 1, "format": "CD", "track-count": 2, "discs": [{"id": "other"}], "tracks": [
              {"position": 1, "title": "Wrong 1", "length": 1000}, {"position": 2, "title": "Wrong 2", "length": 1000}]},
            {"position": 2, "format": "CD", "track-count": 6, "discs": [{"id": "49HHV7Eb8UKF3aQiNmu1GR8vKTY-"}], "tracks": [
              {"position": 1, "title": "One", "length": 201000},
              {"position": 2, "title": "Two", "length": 180500, "artist-credit": [{"name": "Guest", "joinphrase": ""}]},
              {"position": 3, "title": "Three", "length": 0, "recording": {"title": "Three", "length": 99000}},
              {"position": 4, "title": "Four"}, {"position": 5, "title": "Five"}, {"position": 6, "title": "Six"}]}
          ]},
        { "id": "rel-gb", "title": "Test Album", "status": "Official", "date": "1999", "country": "GB",
          "artist-credit": [{"name": "Alpha", "joinphrase": " & "}, {"name": "Beta", "joinphrase": ""}],
          "media": [{"position": 2, "format": "CD", "track-count": 6, "discs": [{"id": "49HHV7Eb8UKF3aQiNmu1GR8vKTY-"}],
                     "tracks": [{"title": "One"}, {"title": "Two"}, {"title": "Three"}, {"title": "Four"}, {"title": "Five"}, {"title": "Six"}]},
                    {"position": 1, "format": "CD", "track-count": 2}]}
      ]})";
    CHECK(ln::json::parse(mbDisc, v, &err));
    std::vector<ln::AlbumMeta> cands;
    CHECK(ln::parseMbDiscResponse(v, "49HHV7Eb8UKF3aQiNmu1GR8vKTY-", 6, cands));
    CHECK(cands.size() == 2);
    if (cands.size() == 2) {
        const auto& a = cands[0];
        CHECK(a.kind == ln::MatchKind::DiscId);
        CHECK_EQ(a.artist, std::string("Alpha & Beta"));
        CHECK_EQ(a.label, std::string("Label One"));
        CHECK_EQ(a.genre, std::string("Rock"));
        CHECK(a.discNumber == 2 && a.discCount == 2);
        CHECK_EQ(a.displayTitle(), std::string("Test Album (Disc 2)"));
        CHECK(a.tracks.size() == 6);
        CHECK_EQ(a.tracks[0].title, std::string("One"));
        CHECK_EQ(a.trackArtist(1), std::string("Guest"));
        CHECK_EQ(a.trackArtist(0), std::string("Alpha & Beta"));
        CHECK(a.tracks[2].lengthMs == 99000);
        CHECK_EQ(a.year(), std::string("1999"));
        CHECK_EQ(a.originalYear(), std::string("1998"));
        CHECK_EQ(cands[1].originalYear(), std::string("1999"));
        CHECK(ln::candidatesEquivalent(cands));
        ln::rankCandidates(cands, "GB");
        CHECK_EQ(cands[0].id, std::string("rel-gb"));
    }

    // --- Fuzzy TOC hit (bare release list) and CD stub
    CHECK(ln::json::parse(R"({"release-count":1,"release-offset":0,"releases":[{"id":"x","title":"T","media":[{"format":"CD","track-count":6,"tracks":[{"title":"a"},{"title":"b"},{"title":"c"},{"title":"d"},{"title":"e"},{"title":"f"}]}]}]})", v, &err));
    cands.clear();
    CHECK(ln::parseMbDiscResponse(v, "zzz", 6, cands));
    CHECK(cands.size() == 1 && cands[0].kind == ln::MatchKind::Fuzzy);
    CHECK(ln::json::parse(R"({"id":"stub","title":"Demo","artist":"Band","track-count":2,"tracks":[{"title":"x","length":1},{"title":"y","artist":"Feat"}]})", v, &err));
    cands.clear();
    CHECK(ln::parseMbDiscResponse(v, "stub", 2, cands));
    CHECK(cands.size() == 1 && cands[0].kind == ln::MatchKind::CdStub && cands[0].trackArtist(1) == "Feat");

    // --- Search response filters by medium track count
    CHECK(ln::json::parse(R"({"releases":[{"id":"a","score":100,"title":"X","media":[{"format":"CD","track-count":6,"position":1}]},
                                          {"id":"b","score":90,"title":"Y","media":[{"format":"CD","track-count":9}]}]})", v, &err));
    cands.clear();
    CHECK(ln::parseMbSearchResponse(v, 6, cands));
    CHECK(cands.size() == 1 && cands[0].id == "a" && !cands[0].tracksLoaded);
    CHECK_EQ(ln::mbSearchQuery("AC/DC", "Back in Black", 10),
             std::string("release:(Back in Black) AND artist:(AC\\/DC) AND tracksmedium:10"));
    CHECK_EQ(ln::urlEncode("a b+c/é"), std::string("a%20b%2Bc%2F%C3%A9"));

    // --- GnuDB
    std::vector<ln::GnuMatch> gm;
    CHECK(ln::parseGnuQuery("200 rock 3404f606 Alpha / Test Album\r\n", gm));
    CHECK(gm.size() == 1 && gm[0].exact && gm[0].category == "rock");
    gm.clear();
    CHECK(ln::parseGnuQuery("211 Found inexact matches\r\nrock 11111111 A / B\r\nmisc 22222222 C / D\r\n.\r\n", gm));
    CHECK(gm.size() == 2 && !gm[0].exact);
    gm.clear();
    CHECK(ln::parseGnuQuery("202 No match found\r\n", gm));
    CHECK(gm.empty());
    ln::AlbumMeta g;
    CHECK(ln::parseGnuRead("210 rock 3404f606 CD database entry follows\r\n# xmcd\r\nDISCID=3404f606\r\n"
                           "DTITLE=Various Artists / Mix\r\nDYEAR=2001\r\nDGENRE=Pop\r\nTTITLE0=Artist A / Song A\r\n"
                           "TTITLE1=Long title part one\r\nTTITLE1= and part two\r\n.\r\n", g));
    CHECK_EQ(g.title, std::string("Mix"));
    CHECK_EQ(g.trackArtist(0), std::string("Artist A"));
    CHECK_EQ(g.tracks[0].title, std::string("Song A"));
    CHECK_EQ(g.tracks[1].title, std::string("Long title part one and part two"));

    // --- Placeholder detection
    CHECK(ln::looksLikePlaceholderTitles({"Track 1", "Track 2", "Track 3"}));
    CHECK(ln::looksLikePlaceholderTitles({"Piste 01", "Piste 02"}));
    CHECK(!ln::looksLikePlaceholderTitles({"Song 2", "Beetlebum"}));
    CHECK(!ln::looksLikePlaceholderTitles({"Track 1", "Real Title"}));

    // --- Cache round trip
    ln::AlbumMeta rt;
    CHECK(ln::albumFromJson(ln::albumToJson(g), rt));
    CHECK(rt.kind == ln::MatchKind::Cache && rt.tracks.size() == 2 && rt.tracks[1].title == g.tracks[1].title);

    // --- original release date survives the cache
    ln::AlbumMeta dated = g;
    dated.originalDate = "1987-05-01";
    CHECK(ln::albumFromJson(ln::albumToJson(dated), rt));
    CHECK_EQ(rt.originalDate, std::string("1987-05-01"));
    CHECK_EQ(rt.originalYear(), std::string("1987"));

    // --- Mixed-mode disc (Half-Life's CD-ROM): data track 1, then 27 audio tracks. WMP lists
    //     the 27 audio tracks; MusicBrainz and GnuDB both count the data track.
    {
        std::vector<int> offs = {150, 130650};               // 29 minutes of game data first
        for (int k = 1; k < 27; ++k) offs.push_back(offs.back() + 9000 + 75 * k);
        std::vector<bool> data(offs.size(), false);
        data[0] = true;
        auto hl = makeToc(1, offs, offs.back() + 9000, data);
        CHECK(hl.valid());
        CHECK(hl.trackCount() == 28 && hl.audioTrackCount() == 27 && hl.mbTrackCount() == 28);
        CHECK(hl.audioTrackIndices().size() == 27 && hl.audioTrackIndices()[0] == 1);
        CHECK(hl.trackLengthMs(1) == 121000);                 // first audio track: 9075 frames

        auto title = [](int audioNo) -> std::string {
            switch (audioNo) {
                case 1: return "Adrenaline Horror";
                case 2: return "Vague Voices (Black Mesa Inbound)";
                case 3: return "Klaxon Beat";
                case 25: return "Threatening Short";
                case 26: return "Dark Piano Short";
                case 27: return "Sharp Fear Short";
                default: return "Cue " + std::to_string(audioNo);
            }
        };
        std::string js = R"({"id":"HL","releases":[{"id":"hl-1998","title":"Half-Life","status":"Official",)"
                         R"("media":[{"position":1,"format":"CD","track-count":28,"discs":[{"id":"HL"}],"tracks":[)"
                         R"({"position":1,"title":"[data track]","length":1740000})";
        for (int k = 1; k <= 27; ++k)
            js += ",{\"position\":" + std::to_string(k + 1) + ",\"title\":\"" + title(k) + "\"" +
                  (k == 2 ? "" : ",\"length\":100000") + "}";      // track 2 has no length on purpose
        js += "]}]}]}";
        CHECK(ln::json::parse(js, v, &err));
        cands.clear();
        CHECK(ln::parseMbDiscResponse(v, "HL", hl.mbTrackCount(), cands, hl.audioTrackCount()));
        CHECK(cands.size() == 1);
        if (cands.size() == 1) {
            ln::AlbumMeta a = cands[0];
            CHECK(a.tracks.size() == 28);
            CHECK(ln::alignToDisc(a, hl) == 1);
            CHECK(a.tracks.size() == 27);
            CHECK_EQ(a.tracks[0].title, std::string("Adrenaline Horror"));
            CHECK_EQ(a.tracks[1].title, std::string("Vague Voices (Black Mesa Inbound)"));
            CHECK_EQ(a.tracks[25].title, std::string("Dark Piano Short"));
            CHECK_EQ(a.tracks[26].title, std::string("Sharp Fear Short"));
            CHECK(a.tracks[0].number == 1 && a.tracks[26].number == 27);
            CHECK(a.tracks[1].lengthMs == hl.trackLengthMs(2));   // filled from the right TOC entry
            CHECK(ln::alignToDisc(a, hl) == 0 && a.tracks.size() == 27);   // idempotent

            // A 1.0.0 cache entry still holds the data track; loading and aligning repairs it.
            ln::AlbumMeta old = cands[0], back;
            CHECK(ln::albumFromJson(ln::albumToJson(old), back));
            CHECK(back.tracks.size() == 28 && ln::alignToDisc(back, hl) == 1);
            CHECK_EQ(back.tracks[0].title, std::string("Adrenaline Horror"));

            // Without a TOC (search fallback), the data-track title gives it away.
            ln::AlbumMeta noToc = cands[0];
            CHECK(ln::dropDataTrackTitles(noToc, 27) == 1 && noToc.tracks.size() == 27);
            CHECK_EQ(noToc.tracks[26].title, std::string("Sharp Fear Short"));
        }

        // Fuzzy TOC hit and CD stub: the medium counts 28, WMP 27; both are still accepted.
        CHECK(ln::json::parse(R"({"releases":[{"id":"f","title":"Half-Life","media":[{"format":"CD","track-count":28}]}]})", v, &err));
        cands.clear();
        CHECK(ln::parseMbDiscResponse(v, "zzz", hl.mbTrackCount(), cands, hl.audioTrackCount()));
        CHECK(cands.size() == 1);

        // Searching and fetching from the window find the 28-track medium too.
        CHECK(ln::json::parse(R"({"releases":[{"id":"s","score":100,"title":"Half-Life","media":[{"format":"CD","track-count":28,"position":1}]}]})", v, &err));
        cands.clear();
        CHECK(ln::parseMbSearchResponse(v, 27, cands, 28));
        CHECK(cands.size() == 1);
        CHECK_EQ(ln::mbSearchQuery("Kelly Bailey", "Half-Life", 27, 28),
                 std::string("release:(Half\\-Life) AND artist:(Kelly Bailey) AND tracksmedium:(27 OR 28)"));
        CHECK_EQ(ln::mbSearchQuery("", "X", 10, 10), std::string("release:(X) AND tracksmedium:10"));

        // GnuDB lists every track, the data track included, with whatever title the submitter chose.
        std::string gnu = "210 misc 12345678 CD database entry follows\r\nDTITLE=Kelly Bailey / Half-Life\r\nTTITLE0=Data\r\n";
        for (int k = 1; k <= 27; ++k) gnu += "TTITLE" + std::to_string(k) + "=" + title(k) + "\r\n";
        gnu += ".\r\n";
        ln::AlbumMeta gd;
        CHECK(ln::parseGnuRead(gnu, gd));
        CHECK(gd.tracks.size() == 28 && ln::alignToDisc(gd, hl) == 1);
        CHECK_EQ(gd.tracks[0].title, std::string("Adrenaline Horror"));
        CHECK(gd.tracks[0].lengthMs == 121000);
    }

    // --- Enhanced CD: MusicBrainz already leaves the trailing data track out, GnuDB doesn't.
    {
        ln::AlbumMeta mb;
        for (int k = 1; k <= 7; ++k) mb.tracks.push_back({k, "Song " + std::to_string(k), "", 0});
        CHECK(ln::alignToDisc(mb, te) == 0 && mb.tracks.size() == 7);
        CHECK(mb.tracks[6].lengthMs == (114574 - 99174) * 1000 / 75);   // ends at the session gap
        ln::AlbumMeta gd = mb;
        gd.tracks.push_back({8, "Enhanced CD", "", 0});
        CHECK(ln::alignToDisc(gd, te) == 1 && gd.tracks.size() == 7);
        CHECK_EQ(gd.tracks[6].title, std::string("Song 7"));
    }

    // --- An ordinary audio CD is left exactly as it was
    {
        ln::AlbumMeta a;
        for (int k = 1; k <= 6; ++k) a.tracks.push_back({k, k == 3 ? "Data" : "Song", "", 1000});
        CHECK(ln::alignToDisc(a, t6) == 0 && a.tracks.size() == 6);
        CHECK_EQ(a.tracks[2].title, std::string("Data"));        // a song can be called "Data"
    }

    // --- Data-track titles
    CHECK(ln::isDataTrackTitle("[data track]"));
    CHECK(ln::isDataTrackTitle(" [Data Track] "));
    CHECK(ln::isDataTrackTitle("Data"));
    CHECK(ln::isDataTrackTitle("(CD-ROM Data)"));
    CHECK(!ln::isDataTrackTitle("Datagram"));
    CHECK(!ln::isDataTrackTitle("Adrenaline Horror"));
    CHECK(ln::isDataTrackTitle("[DATA TRACK]", true));
    CHECK(!ln::isDataTrackTitle("Data", true));

    // --- Nōto's voice
    {
        using namespace ln;
        CHECK_EQ(noto::name(), std::string("N\xC5\x8Dto"));
        struct { Line l; const char* what; Mood mood; } lines[] = {
            {noto::greeting(), "greeting", Mood::Idle},
            {noto::noAudioCd("F:"), "noAudioCd", Mood::Idle},
            {noto::reading(), "reading", Mood::Busy},
            {noto::lookingUp(), "lookingUp", Mood::Busy},
            {noto::tocFailed(), "tocFailed", Mood::Sorry},
            {noto::remembered("Illumina"), "remembered", Mood::Happy},
            {noto::foundOne("OK Computer", "Radiohead"), "foundOne", Mood::Happy},
            {noto::foundMany(4), "foundMany", Mood::Idle},
            {noto::notFound(), "notFound", Mood::Sorry},
            {noto::offline(), "offline", Mood::Sorry},
            {noto::applied("Synthetica", "Metric"), "applied", Mood::Happy},
            {noto::leftAlone(), "leftAlone", Mood::Idle},
            {noto::refused(), "refused", Mood::Sorry},
            {noto::cdGone(), "cdGone", Mood::Sorry},
            {noto::noTracksYet(), "noTracksYet", Mood::Busy},
            {noto::searching(), "searching", Mood::Busy},
            {noto::searchHits(1), "searchHits(1)", Mood::Happy},
            {noto::searchHits(9), "searchHits(9)", Mood::Idle},
            {noto::searchEmpty(13), "searchEmpty", Mood::Sorry},
            {noto::searchFailed(), "searchFailed", Mood::Sorry},
            {noto::needSearchTerms(), "needSearchTerms", Mood::Idle},
            {noto::stillLoading(), "stillLoading", Mood::Busy},
            {noto::tracklistFailed(), "tracklistFailed", Mood::Sorry},
            {noto::threadFailed(), "threadFailed", Mood::Sorry},
        };
        for (const auto& t : lines) {
            checkLine(t.l, t.what);
            if (t.l.mood != t.mood) { std::printf("FAIL voice %s: wrong mood\n", t.what); ++failures; }
        }
        CHECK(noto::chatterCount() >= 5);
        for (unsigned i = 0; i < noto::chatterCount(); ++i) checkLine(noto::chatter(i), "chatter");
        CHECK_EQ(noto::chatter(noto::chatterCount()).text, noto::chatter(0).text);   // cycles
        CHECK(noto::applied("Synthetica", "Metric").text.find("Synthetica by Metric") != std::string::npos);
        CHECK(noto::applied("", "").text.find("your CD") != std::string::npos);
        CHECK(noto::noAudioCd("").text.find("the drive") != std::string::npos);
        ln::Line q{ln::Mood::Idle, noto::trackMismatch(12, 13)};
        checkLine(q, "trackMismatch");
        CHECK(q.text.find("12 tracks") != std::string::npos && q.text.find("has 13") != std::string::npos);
    }

    if (failures == 0) std::printf("All core tests passed.\n");
    return failures ? 1 : 0;
}
