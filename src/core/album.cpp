#include "album.h"
#include "json.h"
#include <algorithm>
#include <cctype>

namespace ln {

const char* matchKindName(MatchKind k) {
    switch (k) {
        case MatchKind::DiscId: return "disc ID";
        case MatchKind::Fuzzy: return "TOC match";
        case MatchKind::CdStub: return "CD stub";
        case MatchKind::GnuDbExact: return "GnuDB exact";
        case MatchKind::GnuDbInexact: return "GnuDB inexact";
        case MatchKind::Search: return "search";
        case MatchKind::Cache: return "saved";
    }
    return "?";
}

static std::string yearOf(const std::string& date) {
    if (date.size() >= 4 && std::isdigit((unsigned char)date[0]) && std::isdigit((unsigned char)date[1]) &&
        std::isdigit((unsigned char)date[2]) && std::isdigit((unsigned char)date[3]))
        return date.substr(0, 4);
    return "";
}

std::string AlbumMeta::originalYear() const {
    std::string y = yearOf(originalDate), mine = year();
    if (y.empty() || (!mine.empty() && mine < y)) return mine;   // guard against bad release-group data
    return y;
}

std::string AlbumMeta::year() const {
    if (date.size() >= 4 && std::isdigit((unsigned char)date[0]) && std::isdigit((unsigned char)date[1]) &&
        std::isdigit((unsigned char)date[2]) && std::isdigit((unsigned char)date[3]))
        return date.substr(0, 4);
    return "";
}

std::string AlbumMeta::displayTitle() const {
    if (discCount > 1) return title + " (Disc " + std::to_string(discNumber) + ")";
    return title;
}

std::string AlbumMeta::trackArtist(size_t i) const {
    if (i < tracks.size() && !tracks[i].artist.empty()) return tracks[i].artist;
    return artist;
}

static std::string norm(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c >= 0x80) o += char(c);                  // keep non-ASCII bytes as-is
        else if (std::isalnum(c)) o += char(std::tolower(c));
    }
    return o;
}

void rankCandidates(std::vector<AlbumMeta>& c, const std::string& preferredCountry) {
    for (auto& a : c) {
        int r = 0;
        switch (a.kind) {
            case MatchKind::Cache: r += 100; break;
            case MatchKind::DiscId: r += 60; break;
            case MatchKind::Fuzzy: r += 35; break;
            case MatchKind::GnuDbExact: r += 30; break;
            case MatchKind::CdStub: r += 15; break;
            case MatchKind::GnuDbInexact: r += 10; break;
            case MatchKind::Search: r += a.searchScore / 5; break;
        }
        if (a.status == "Official") r += 8;
        if (!preferredCountry.empty() && a.country == preferredCountry) r += 5;
        else if (a.country == "XE" || a.country == "XW") r += 3;
        if (a.format.find("CD") != std::string::npos) r += 2;
        if (!a.label.empty()) r += 1;
        a.rank = r;
    }
    std::stable_sort(c.begin(), c.end(), [](const AlbumMeta& x, const AlbumMeta& y) { return x.rank > y.rank; });
}

bool candidatesEquivalent(const std::vector<AlbumMeta>& c) {
    if (c.size() <= 1) return true;
    const AlbumMeta& a = c[0];
    for (size_t i = 1; i < c.size(); ++i) {
        const AlbumMeta& b = c[i];
        if (norm(a.title) != norm(b.title) || norm(a.artist) != norm(b.artist)) return false;
        if (a.tracks.size() != b.tracks.size()) return false;
        for (size_t t = 0; t < a.tracks.size(); ++t)
            if (norm(a.tracks[t].title) != norm(b.tracks[t].title)) return false;
    }
    return true;
}

bool looksLikePlaceholderTitles(const std::vector<std::string>& titles) {
    if (titles.empty()) return true;
    for (size_t i = 0; i < titles.size(); ++i) {
        std::string t = titles[i];
        while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
        if (t.empty()) continue;
        std::string num = std::to_string(i + 1);
        std::string num2 = (i + 1 < 10) ? "0" + num : num;
        auto endsWith = [&](const std::string& n) {
            return t.size() >= n.size() && t.compare(t.size() - n.size(), n.size(), n) == 0;
        };
        std::string prefix;
        if (endsWith(num2)) prefix = t.substr(0, t.size() - num2.size());
        else if (endsWith(num)) prefix = t.substr(0, t.size() - num.size());
        else return false;
        if (prefix.size() > 24) return false;
        for (unsigned char ch : prefix) if (std::isdigit(ch)) return false;
    }
    return true;
}

static std::string lowerTrimmed(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    std::string o = s.substr(b, e - b);
    for (auto& c : o) c = char(std::tolower((unsigned char)c));
    return o;
}

bool isDataTrackTitle(const std::string& title, bool bracketedOnly) {
    std::string t = lowerTrimmed(title);
    if (bracketedOnly) return t == "[data track]";
    if (t.size() >= 2 && ((t.front() == '[' && t.back() == ']') || (t.front() == '(' && t.back() == ')')))
        t = lowerTrimmed(t.substr(1, t.size() - 2));
    static const char* const known[] = {"data track", "data", "data (cd-rom)", "cd-rom data", "cd-rom track",
                                        "cd-rom", "cdrom", "cdrom data", "computer data", "multimedia data",
                                        "multimedia track", "enhanced cd", "cd-extra", "video data"};
    for (const char* k : known)
        if (t == k) return true;
    return false;
}

static void renumber(AlbumMeta& a) {
    for (size_t i = 0; i < a.tracks.size(); ++i) a.tracks[i].number = int(i) + 1;
}

int dropDataTrackTitles(AlbumMeta& a, int audioTracks) {
    int dropped = 0;
    for (size_t i = 0; i < a.tracks.size() && int(a.tracks.size()) > audioTracks;) {
        if (isDataTrackTitle(a.tracks[i].title)) { a.tracks.erase(a.tracks.begin() + long(i)); ++dropped; }
        else ++i;
    }
    if (dropped) renumber(a);
    return dropped;
}

int alignToDisc(AlbumMeta& a, const DiscToc& toc) {
    const int audio = toc.audioTrackCount();
    const int n = int(a.tracks.size());
    int dropped = 0;
    if (n > audio && audio < toc.trackCount() && (n == toc.trackCount() || n == toc.mbTrackCount())) {
        // The source counts physical tracks from the first one: drop the ones the TOC marks as data.
        std::vector<TrackMeta> kept;
        for (int i = 0; i < n; ++i)
            if (!(size_t(i) < toc.isData.size() && toc.isData[size_t(i)])) kept.push_back(a.tracks[size_t(i)]);
        dropped = n - int(kept.size());
        a.tracks.swap(kept);
        if (dropped) renumber(a);
    } else if (n > audio) {
        dropped = dropDataTrackTitles(a, audio);
    }
    if (int(a.tracks.size()) == audio) {
        const std::vector<int> idx = toc.audioTrackIndices();
        for (size_t i = 0; i < a.tracks.size(); ++i)
            if (a.tracks[i].lengthMs <= 0) a.tracks[i].lengthMs = toc.trackLengthMs(size_t(idx[i]));
    }
    return dropped;
}

std::string formatDuration(int ms) {
    if (ms <= 0) return "";
    int s = (ms + 500) / 1000;
    return std::to_string(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + std::to_string(s % 60);
}

std::string albumToJson(const AlbumMeta& a) {
    using json::quote;
    std::string o = "{\n";
    auto kv = [&](const char* k, const std::string& v) { o += "  " + quote(k) + ": " + quote(v) + ",\n"; };
    kv("source", a.source); kv("id", a.id); kv("title", a.title); kv("artist", a.artist);
    kv("date", a.date); kv("country", a.country); kv("label", a.label); kv("catalogNumber", a.catalogNumber);
    kv("barcode", a.barcode); kv("genre", a.genre); kv("status", a.status);
    kv("disambiguation", a.disambiguation); kv("format", a.format);
    kv("originalDate", a.originalDate);
    o += "  \"discNumber\": " + std::to_string(a.discNumber) + ",\n";
    o += "  \"discCount\": " + std::to_string(a.discCount) + ",\n";
    o += "  \"tracks\": [\n";
    for (size_t i = 0; i < a.tracks.size(); ++i) {
        const auto& t = a.tracks[i];
        o += "    {\"number\": " + std::to_string(t.number) + ", \"title\": " + quote(t.title) +
             ", \"artist\": " + quote(t.artist) + ", \"lengthMs\": " + std::to_string(t.lengthMs) + "}";
        o += (i + 1 < a.tracks.size()) ? ",\n" : "\n";
    }
    o += "  ]\n}\n";
    return o;
}

bool albumFromJson(const std::string& text, AlbumMeta& out) {
    json::Value v;
    if (!json::parse(text, v) || !v.isObject()) return false;
    AlbumMeta a;
    a.kind = MatchKind::Cache;
    a.source = v["source"].str(); a.id = v["id"].str(); a.title = v["title"].str(); a.artist = v["artist"].str();
    a.date = v["date"].str(); a.country = v["country"].str(); a.label = v["label"].str();
    a.catalogNumber = v["catalogNumber"].str(); a.barcode = v["barcode"].str(); a.genre = v["genre"].str();
    a.status = v["status"].str(); a.disambiguation = v["disambiguation"].str(); a.format = v["format"].str();
    a.discNumber = v["discNumber"].integer(1); a.discCount = v["discCount"].integer(1);
    a.originalDate = v["originalDate"].str();
    const auto& tr = v["tracks"];
    for (size_t i = 0; i < tr.size(); ++i) {
        TrackMeta t;
        t.number = tr[i]["number"].integer(int(i) + 1);
        t.title = tr[i]["title"].str(); t.artist = tr[i]["artist"].str(); t.lengthMs = tr[i]["lengthMs"].integer();
        a.tracks.push_back(t);
    }
    if (a.title.empty() || a.tracks.empty()) return false;
    out = a;
    return true;
}

} // namespace ln
