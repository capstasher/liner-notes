#include "sources.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <sstream>

namespace ln {

// ---------------------------------------------------------------- MusicBrainz

std::string mbArtistCredit(const json::Value& credits) {
    std::string s;
    for (size_t i = 0; i < credits.size(); ++i) {
        const auto& c = credits[i];
        std::string name = c["name"].str();
        if (name.empty()) name = c["artist"]["name"].str();
        s += name + c["joinphrase"].str();
    }
    return s;
}

static std::string topGenre(const json::Value& genres) {
    std::string best; int bestCount = -1;
    for (size_t i = 0; i < genres.size(); ++i) {
        int n = genres[i]["count"].integer();
        if (n > bestCount) { bestCount = n; best = genres[i]["name"].str(); }
    }
    if (!best.empty()) best[0] = char(std::toupper((unsigned char)best[0]));
    return best;
}

static bool mediumHasDisc(const json::Value& medium, const std::string& discId) {
    const auto& discs = medium["discs"];
    for (size_t i = 0; i < discs.size(); ++i)
        if (discs[i]["id"].str() == discId) return true;
    return false;
}

bool parseMbRelease(const json::Value& r, const std::string& discId, int trackCount,
                    MatchKind kind, AlbumMeta& out) {
    if (!r.isObject()) return false;
    const auto& media = r["media"];
    int pick = -1;
    // 1) the medium that carries our exact disc ID
    if (!discId.empty())
        for (size_t i = 0; i < media.size() && pick < 0; ++i)
            if (mediumHasDisc(media[i], discId)) pick = int(i);
    // 2) a CD-ish medium with the right number of tracks
    for (size_t i = 0; i < media.size() && pick < 0; ++i) {
        int tc = media[i]["track-count"].integer(int(media[i]["tracks"].size()));
        if (tc == trackCount && media[i]["format"].str().find("CD") != std::string::npos) pick = int(i);
    }
    // 3) any medium with the right number of tracks
    for (size_t i = 0; i < media.size() && pick < 0; ++i) {
        int tc = media[i]["track-count"].integer(int(media[i]["tracks"].size()));
        if (tc == trackCount) pick = int(i);
    }
    if (pick < 0) return false;
    const auto& m = media[size_t(pick)];

    AlbumMeta a;
    a.kind = kind;
    a.source = "MusicBrainz";
    a.id = r["id"].str();
    a.title = r["title"].str();
    a.artist = mbArtistCredit(r["artist-credit"]);
    a.date = r["date"].str();
    a.originalDate = r["release-group"]["first-release-date"].str();
    if (a.date.empty()) a.date = a.originalDate;
    a.country = r["country"].str();
    a.barcode = r["barcode"].str();
    a.status = r["status"].str();
    a.disambiguation = r["disambiguation"].str();
    const auto& li = r["label-info"];
    for (size_t i = 0; i < li.size(); ++i) {
        if (a.label.empty()) a.label = li[i]["label"]["name"].str();
        if (a.catalogNumber.empty()) a.catalogNumber = li[i]["catalog-number"].str();
    }
    a.genre = topGenre(r["genres"]);
    if (a.genre.empty()) a.genre = topGenre(r["release-group"]["genres"]);
    a.format = m["format"].str();
    a.discNumber = m["position"].integer(pick + 1);
    a.discCount = int(media.size());
    if (a.discCount < 1) a.discCount = 1;

    const auto& tracks = m["tracks"];
    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];
        TrackMeta tm;
        tm.number = t["position"].integer(int(i) + 1);
        tm.title = t["title"].str();
        if (tm.title.empty()) tm.title = t["recording"]["title"].str();
        tm.artist = mbArtistCredit(t["artist-credit"]);
        if (tm.artist.empty()) tm.artist = mbArtistCredit(t["recording"]["artist-credit"]);
        tm.lengthMs = t["length"].integer();
        if (tm.lengthMs <= 0) tm.lengthMs = t["recording"]["length"].integer();
        a.tracks.push_back(tm);
    }
    a.tracksLoaded = !a.tracks.empty();
    out = a;
    return true;
}

bool parseMbDiscResponse(const json::Value& root, const std::string& discId, int trackCount,
                         std::vector<AlbumMeta>& out) {
    if (!root.isObject()) return false;
    if (const json::Value* rel = root.find("releases")) {
        // A "disc" object (exact hit) carries its own "id"; a fuzzy TOC hit is a bare release list.
        MatchKind kind = root.find("id") ? MatchKind::DiscId : MatchKind::Fuzzy;
        for (size_t i = 0; i < rel->size(); ++i) {
            AlbumMeta a;
            if (parseMbRelease((*rel)[i], discId, trackCount, kind, a)) out.push_back(a);
        }
        return true;
    }
    if (root.find("tracks") && root.find("title")) {   // CD stub
        AlbumMeta a;
        a.kind = MatchKind::CdStub;
        a.source = "MusicBrainz CD stub";
        a.id = root["id"].str();
        a.title = root["title"].str();
        a.artist = root["artist"].str();
        a.barcode = root["barcode"].str();
        a.disambiguation = root["disambiguation"].str();
        a.format = "CD";
        const auto& tr = root["tracks"];
        for (size_t i = 0; i < tr.size(); ++i) {
            TrackMeta t;
            t.number = int(i) + 1;
            t.title = tr[i]["title"].str();
            t.artist = tr[i]["artist"].str();
            t.lengthMs = tr[i]["length"].integer();
            a.tracks.push_back(t);
        }
        if (int(a.tracks.size()) == trackCount || trackCount <= 0) out.push_back(a);
        return true;
    }
    return false;
}

bool parseMbSearchResponse(const json::Value& root, int trackCount, std::vector<AlbumMeta>& out) {
    const auto& rel = root["releases"];
    if (!rel.isArray()) return false;
    for (size_t i = 0; i < rel.size(); ++i) {
        const auto& r = rel[i];
        AlbumMeta a;
        a.kind = MatchKind::Search;
        a.source = "MusicBrainz";
        a.id = r["id"].str();
        a.title = r["title"].str();
        a.artist = mbArtistCredit(r["artist-credit"]);
        a.date = r["date"].str();
        a.country = r["country"].str();
        a.status = r["status"].str();
        a.barcode = r["barcode"].str();
        a.disambiguation = r["disambiguation"].str();
        a.searchScore = r["score"].integer();
        const auto& li = r["label-info"];
        if (li.size()) { a.label = li[0]["label"]["name"].str(); a.catalogNumber = li[0]["catalog-number"].str(); }
        const auto& media = r["media"];
        a.discCount = media.size() ? int(media.size()) : 1;
        bool fits = trackCount <= 0;
        for (size_t m = 0; m < media.size(); ++m) {
            if (media[m]["track-count"].integer() == trackCount) {
                fits = true;
                a.format = media[m]["format"].str();
                a.discNumber = media[m]["position"].integer(int(m) + 1);
                break;
            }
        }
        if (!fits) continue;   // no medium with our track count: cannot be this disc
        a.tracksLoaded = false;
        out.push_back(a);
    }
    return true;
}

std::string urlEncode(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += char(c);
        else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
    }
    return o;
}

std::string luceneEscape(const std::string& s) {
    static const std::string special = "+-&|!(){}[]^\"~*?:\\/";
    std::string o;
    for (char c : s) { if (special.find(c) != std::string::npos) o += '\\'; o += c; }
    return o;
}

std::string mbSearchQuery(const std::string& artist, const std::string& album, int trackCount) {
    std::vector<std::string> parts;
    if (!album.empty()) parts.push_back("release:(" + luceneEscape(album) + ")");
    if (!artist.empty()) parts.push_back("artist:(" + luceneEscape(artist) + ")");
    if (trackCount > 0) parts.push_back("tracksmedium:" + std::to_string(trackCount));
    std::string q;
    for (size_t i = 0; i < parts.size(); ++i) q += (i ? " AND " : "") + parts[i];
    return q;
}

// ---------------------------------------------------------------- GnuDB

static std::vector<std::string> splitLines(const std::string& body) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : body) {
        if (c == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back(); lines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) { if (cur.back() == '\r') cur.pop_back(); lines.push_back(cur); }
    return lines;
}

static bool parseMatchLine(const std::string& line, GnuMatch& m) {
    // "categ discid dtitle"
    size_t a = line.find(' ');
    if (a == std::string::npos) return false;
    size_t b = line.find(' ', a + 1);
    if (b == std::string::npos) return false;
    m.category = line.substr(0, a);
    m.discId = line.substr(a + 1, b - a - 1);
    m.dtitle = line.substr(b + 1);
    return !m.category.empty() && !m.discId.empty();
}

bool parseGnuQuery(const std::string& body, std::vector<GnuMatch>& out, std::string* status) {
    auto lines = splitLines(body);
    if (lines.empty()) return false;
    const std::string& head = lines[0];
    if (status) *status = head;
    int code = std::atoi(head.c_str());
    if (code == 202) return true;                        // no match
    if (code == 200) {                                   // "200 categ discid dtitle"
        GnuMatch m;
        if (head.size() > 4 && parseMatchLine(head.substr(4), m)) { m.exact = true; out.push_back(m); return true; }
        return false;
    }
    if (code == 210 || code == 211) {
        for (size_t i = 1; i < lines.size(); ++i) {
            if (lines[i] == ".") break;
            GnuMatch m;
            if (parseMatchLine(lines[i], m)) { m.exact = (code == 210); out.push_back(m); }
        }
        return true;
    }
    return false;
}

static void splitArtistTitle(const std::string& s, std::string& artist, std::string& title) {
    size_t p = s.find(" / ");
    if (p == std::string::npos) { artist.clear(); title = s; return; }
    artist = s.substr(0, p);
    title = s.substr(p + 3);
}

bool parseGnuRead(const std::string& body, AlbumMeta& out) {
    auto lines = splitLines(body);
    if (lines.empty() || std::atoi(lines[0].c_str()) != 210) return false;
    std::map<std::string, std::string> kv;
    std::vector<std::string> order;
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        if (l == ".") break;
        if (l.empty() || l[0] == '#') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (!kv.count(k)) order.push_back(k);
        kv[k] += v;                                       // long values continue on repeated keys
    }
    AlbumMeta a;
    a.source = "GnuDB";
    std::string dArtist, dTitle;
    splitArtistTitle(kv["DTITLE"], dArtist, dTitle);
    a.artist = dArtist.empty() ? dTitle : dArtist;
    a.title = dTitle;
    a.date = kv["DYEAR"];
    a.genre = kv["DGENRE"];
    a.format = "CD";
    std::string lowerArtist = a.artist;
    std::transform(lowerArtist.begin(), lowerArtist.end(), lowerArtist.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    bool various = lowerArtist.find("various") != std::string::npos;
    for (int n = 0;; ++n) {
        auto it = kv.find("TTITLE" + std::to_string(n));
        if (it == kv.end()) break;
        TrackMeta t;
        t.number = n + 1;
        if (various && it->second.find(" / ") != std::string::npos) splitArtistTitle(it->second, t.artist, t.title);
        else t.title = it->second;
        a.tracks.push_back(t);
    }
    if (a.title.empty() || a.tracks.empty()) return false;
    out = a;
    return true;
}

} // namespace ln
