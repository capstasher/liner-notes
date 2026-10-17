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
    // Tracks MusicBrainz lists for this disc: up to the last audio track, so a leading data
    // track (mixed-mode game CDs such as Half-Life) is counted and trailing ones are not.
    int mbTrackCount() const { return mbLastTrack() - firstTrack + 1; }
    int audioTrackCount() const;  // tracks WMP will show in its CD playlist
    // Indices into `offsets` of the audio tracks, in disc order: one per WMP playlist item.
    std::vector<int> audioTrackIndices() const;
    // Length of the track at index i, from the TOC. The last audio track before a trailing
    // data session ends at the session gap, not at the data track.
    int trackLengthMs(size_t i) const;
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

// Compact round-trippable form for the cache: "first last leadout off1 off2d ..." ('d' = data).
std::string tocToString(const DiscToc& toc);
bool tocFromString(const std::string& s, DiscToc& out);

// Parse WMP's WM/MCDI ("TOC") attribute string as a fallback TOC source.
// Expected: hex values joined by '+': count, offsets..., lead-out.
bool parseWmpMcdi(const std::string& mcdi, DiscToc& out);

} // namespace ln
