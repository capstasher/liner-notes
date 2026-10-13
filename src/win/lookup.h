// Blocking lookups (call from worker threads only).
#pragma once
#include "../core/album.h"
#include "../core/discid.h"
#include <string>
#include <vector>

namespace ln {

enum class LookupOutcome { None, TocFailed, Cached, Found, NotFound, NetProblem };

struct DiscLookup {
    std::wstring drive;              // "D:"
    DiscToc toc;
    bool tocOk = false;
    std::string mbDiscId, freedbId;
    std::vector<AlbumMeta> candidates;  // ranked best-first
    std::string status;                 // one-line, user-facing
    bool fromCache = false;
    LookupOutcome outcome = LookupOutcome::None;   // drives what Noto says; `status` is the plain log line
};

// Reads the TOC if not already set, then: cache -> MusicBrainz (disc ID + fuzzy TOC + CD stubs) -> GnuDB.
void lookupDisc(DiscLookup& d, bool useCache);

bool searchReleases(const std::string& artist, const std::string& album, int trackCount,
                    std::vector<AlbumMeta>& out, std::string& error);
bool fetchRelease(const std::string& mbid, int trackCount, AlbumMeta& out, std::string& error);

void saveToCache(const std::string& mbDiscId, const AlbumMeta& a);
bool loadFromCache(const std::string& mbDiscId, AlbumMeta& out);
void forgetCache(const std::string& mbDiscId);

std::string userAgent();

} // namespace ln
