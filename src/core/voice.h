// Nōto's voice: what the LinerNotes window says, and how she looks saying it.
//
// The log stays plain on purpose (it is what bug reports are read from); only the
// window speaks in character. Every line the window can show is produced here, so
// her voice lives in one place and is covered by the host-side tests.
#pragma once
#include <string>

namespace ln {

enum class Mood { Idle, Busy, Happy, Sorry };

struct Line {
    Mood mood = Mood::Idle;
    std::string text;          // UTF-8
    bool empty() const { return text.empty(); }
};

namespace noto {

Line greeting();                                   // window opened before any disc is known
Line noAudioCd(const std::string& drive);          // drive is empty or holds a data disc
Line reading();
Line lookingUp();
Line tocFailed();
Line remembered(const std::string& album);         // answered from the local cache
Line foundOne(const std::string& album, const std::string& artist);
Line foundMany(int count);
Line notFound();
Line offline();                                    // MusicBrainz unreachable or erroring
Line applied(const std::string& album, const std::string& artist);
Line leftAlone();                                  // WMP already had real titles
Line refused();                                    // WMP rejected the writes
Line cdGone();
Line noTracksYet();
Line searching();
Line searchHits(int count);
Line searchEmpty(int discTracks);
Line searchFailed();
Line needSearchTerms();
Line stillLoading();
Line tracklistFailed();
Line threadFailed();

// Yes/no question shown before applying a release whose track count differs.
std::string trackMismatch(int releaseTracks, int cdTracks);

// What she says when you click her. Cycles; n can be any counter.
Line chatter(unsigned n);
unsigned chatterCount();

std::string name();                                // "Nōto"

} // namespace noto
} // namespace ln
