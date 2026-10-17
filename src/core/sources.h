// Response parsers for MusicBrainz (JSON web service v2) and GnuDB (CDDB over HTTP),
// plus request-building helpers. No networking here: that lives in src/win/lookup.cpp.
#pragma once
#include "album.h"
#include "json.h"
#include <string>
#include <vector>

namespace ln {

// ---- MusicBrainz -------------------------------------------------------------
// Media are matched on trackCount, or on altTrackCount when that's non-zero: on a mixed-mode
// disc MusicBrainz counts the leading data track (DiscToc::mbTrackCount) and WMP doesn't.
// Run alignToDisc() on the results before mapping tracks onto WMP's playlist.

// GET /ws/2/discid/<id>?toc=...&inc=...  -> exact disc-ID hits, fuzzy TOC hits, or a CD stub
bool parseMbDiscResponse(const json::Value& root, const std::string& discId, int trackCount,
                         std::vector<AlbumMeta>& out, int altTrackCount = 0);
// GET /ws/2/release?query=...            -> search hits without tracklists
bool parseMbSearchResponse(const json::Value& root, int trackCount, std::vector<AlbumMeta>& out,
                           int altTrackCount = 0);
// GET /ws/2/release/<mbid>?inc=...       -> a single full release
bool parseMbRelease(const json::Value& release, const std::string& discId, int trackCount,
                    MatchKind kind, AlbumMeta& out, int altTrackCount = 0);

std::string mbArtistCredit(const json::Value& credits);
std::string urlEncode(const std::string& s);        // RFC 3986 unreserved kept
std::string luceneEscape(const std::string& s);
std::string mbSearchQuery(const std::string& artist, const std::string& album, int trackCount,
                          int altTrackCount = 0);

// ---- GnuDB -------------------------------------------------------------------
struct GnuMatch { std::string category, discId, dtitle; bool exact = false; };
// Response to "cddb query": 200 (one exact), 210/211 (list), 202 (none)
bool parseGnuQuery(const std::string& body, std::vector<GnuMatch>& out, std::string* status = nullptr);
// Response to "cddb read": "210 ..." header followed by an xmcd record
bool parseGnuRead(const std::string& body, AlbumMeta& out);

} // namespace ln
