// Album/track model shared by every metadata source, plus ranking and
// (de)serialisation for the local disc cache.
#pragma once
#include <string>
#include <vector>

namespace ln {

enum class MatchKind { DiscId, Fuzzy, CdStub, GnuDbExact, GnuDbInexact, Search, Cache };
const char* matchKindName(MatchKind k);

struct TrackMeta {
    int number = 0;          // 1-based position on the medium
    std::string title;
    std::string artist;      // may be empty -> album artist
    int lengthMs = 0;
};

struct AlbumMeta {
    MatchKind kind = MatchKind::DiscId;
    std::string source;          // "MusicBrainz", "MusicBrainz CD stub", "GnuDB", ...
    std::string id;              // MusicBrainz release MBID, or "category/discid" for GnuDB
    std::string title, artist, date, country, label, catalogNumber, barcode;
    std::string originalDate;    // release group's first release date (MusicBrainz)
    std::string genre, status, disambiguation, format;
    int discNumber = 1, discCount = 1;
    int searchScore = 0;         // MusicBrainz search relevance (0-100) when kind == Search
    bool tracksLoaded = true;    // false for search hits until the release is fetched
    std::vector<TrackMeta> tracks;
    int rank = 0;                // filled by rankCandidates()

    std::string year() const;                  // this pressing's year
    std::string originalYear() const;          // first release of the album; falls back to year()
    std::string displayTitle() const;          // title plus " (Disc n)" for multi-disc sets
    std::string trackArtist(size_t i) const;   // falls back to album artist
};

// Scores and sorts candidates best-first. preferredCountry: ISO 3166 code ("GB").
void rankCandidates(std::vector<AlbumMeta>& c, const std::string& preferredCountry);

// True when every candidate carries the same album/artist/track titles, i.e.
// they are pressings of one album and picking the top one is safe.
bool candidatesEquivalent(const std::vector<AlbumMeta>& c);

// Heuristic: are these WMP's placeholder titles ("Track 1", "Piste 2", ...)?
bool looksLikePlaceholderTitles(const std::vector<std::string>& titles);

std::string albumToJson(const AlbumMeta& a);
bool albumFromJson(const std::string& text, AlbumMeta& out);

std::string formatDuration(int ms);   // "m:ss"

} // namespace ln
