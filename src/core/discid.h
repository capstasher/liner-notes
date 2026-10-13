// Disc table-of-contents model and the two disc-ID schemes we query with:
// MusicBrainz Disc ID and the classic freedb/CDDB ID (used by GnuDB).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ln {

struct DiscToc {
    int firstTrack = 1;          // physical first track number
    int lastTrack = 0;           // physical last track number
    std::vector<int> offsets;    // absolute frame offsets (LBA + 150), one per physical track
    std::vector<bool> isData;    // control bit 0x04 per physical track
    int leadout = 0;             // absolute frame offset of the physical lead-out

    bool valid() const;
    int trackCount() const { return int(offsets.size()); }

    // MusicBrainz ignores trailing data tracks (Enhanced CD / CD-Extra) and
    // moves the lead-out to (start of first trailing data track - 11400).
    int mbLastTrack() const;
    int mbLeadout() const;
    int audioTrackCount() const;  // tracks WMP will show in its CD playlist
};

std::string musicBrainzDiscId(const DiscToc& toc);
// "first+last+leadout+off1+off2..." for the ?toc= fuzzy-lookup parameter
std::string musicBrainzTocParam(const DiscToc& toc);
uint32_t freedbDiscId(const DiscToc& toc);
std::string freedbDiscIdHex(const DiscToc& toc);
// "discid+ntrks+off1+...+nsecs" for the GnuDB `cddb query` command
std::string gnudbQueryArgs(const DiscToc& toc);
// Human-readable summary for logs
std::string describeToc(const DiscToc& toc);

// Parse WMP's WM/MCDI ("TOC") attribute string as a fallback TOC source.
// Expected: hex values joined by '+': count, offsets..., lead-out.
bool parseWmpMcdi(const std::string& mcdi, DiscToc& out);

} // namespace ln
