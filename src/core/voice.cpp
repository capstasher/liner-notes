// Nōto: a diligent, slightly shy record-shop girl who reads the liner notes so you
// don't have to. Polite to the services she asks (one MusicBrainz question a second),
// never scribbles over titles you already have, keeps everything in her notebook.
//
// Style rules for her lines: British English; short sentences; kaomoji only at
// emotional beats, and only from glyphs every Windows install can draw (no emoji).
#include "voice.h"

namespace ln {
namespace noto {

namespace {
Line say(Mood m, std::string t) { return Line{m, std::move(t)}; }

std::string albumBy(const std::string& album, const std::string& artist) {
    if (album.empty()) return artist.empty() ? std::string("your CD") : artist;
    return artist.empty() ? album : album + " by " + artist;
}
} // namespace

std::string name() { return "N\xC5\x8Dto"; }   // Nōto (U+014D)

Line greeting() {
    return say(Mood::Idle, "Hi, I'm " + name() + "! Pop a CD in and I'll read its liner notes for you \xE2\x99\xAA");
}

Line noAudioCd(const std::string& drive) {
    std::string where = drive.empty() ? std::string("the drive") : drive;
    return say(Mood::Idle, "There's no audio CD in " + where + " yet. I'll be right here when there is!");
}

Line reading() { return say(Mood::Busy, "Reading the disc\xE2\x80\xA6 one moment!"); }

Line lookingUp() {
    return say(Mood::Busy, "Asking MusicBrainz about this disc\xE2\x80\xA6 (\xEF\xBD\x80\xE3\x83\xBB\xCF\x89\xE3\x83\xBB\xC2\xB4)");
}

Line tocFailed() {
    return say(Mood::Sorry, "I couldn't read this disc's table of contents\xE2\x80\xA6 Is it still spinning up? "
                            "Try \"Look again\" in a moment.");
}

Line remembered(const std::string& album) {
    return say(Mood::Happy, "Oh, I remember this one! " + (album.empty() ? std::string("Filling it in") : album) +
                            ", straight from my notebook \xE2\x99\xAA");
}

Line foundOne(const std::string& album, const std::string& artist) {
    return say(Mood::Happy, "Found it! This is " + albumBy(album, artist) + " (\xE2\x89\xA7\xE2\x96\xBD\xE2\x89\xA6)");
}

Line foundMany(int count) {
    return say(Mood::Idle, "I found " + std::to_string(count) + " releases that match. Which one is your copy?");
}

Line notFound() {
    return say(Mood::Sorry, "I couldn't find this disc anywhere\xE2\x80\xA6 (\xEF\xBC\x9B\xCF\x89\xEF\xBC\x9B) "
                            "Could you tell me the artist or album below?");
}

Line offline() {
    return say(Mood::Sorry, "I can't reach MusicBrainz right now. Is the internet napping? "
                            "(\xC2\xB4\xE3\x83\xBB\xCF\x89\xE3\x83\xBB`)");
}

Line applied(const std::string& album, const std::string& artist) {
    return say(Mood::Happy, "Done! Your CD is " + albumBy(album, artist) + " \xE2\x99\xAA");
}

Line leftAlone() {
    return say(Mood::Idle, "WMP already has titles for this disc, so I left them alone. "
                           "Press Apply if you'd like mine instead.");
}

Line refused() {
    return say(Mood::Sorry, "WMP wouldn't let me write the titles\xE2\x80\xA6 I've noted why in my notebook.");
}

Line cdGone() { return say(Mood::Sorry, "Eh? The CD's gone!"); }

Line noTracksYet() {
    return say(Mood::Busy, "WMP hasn't listed this CD's tracks yet. Give it a second and try again?");
}

Line searching() { return say(Mood::Busy, "Searching MusicBrainz\xE2\x80\xA6 \xE2\x9C\xA7"); }

Line searchHits(int count) {
    if (count == 1) return say(Mood::Happy, "I found one! Check the tracklist, then Apply.");
    return say(Mood::Idle, "Here are " + std::to_string(count) +
                           " releases with the right number of tracks. Pick one to see its tracklist.");
}

Line searchEmpty(int discTracks) {
    return say(Mood::Sorry, "Nothing with a " + std::to_string(discTracks) +
                            "-track disc\xE2\x80\xA6 Maybe try fewer words?");
}

Line searchFailed() {
    return say(Mood::Sorry, "The search didn't go through. Could you try again in a moment?");
}

Line needSearchTerms() { return say(Mood::Idle, "Tell me an artist or an album and I'll go and look!"); }

Line stillLoading() { return say(Mood::Busy, "Wait, I'm still fetching that tracklist\xE2\x80\xA6"); }

Line tracklistFailed() { return say(Mood::Sorry, "I couldn't load that tracklist\xE2\x80\xA6 sorry!"); }

Line threadFailed() {
    return say(Mood::Sorry, "Something went wrong on my end and I couldn't start looking. Sorry!");
}

std::string trackMismatch(int releaseTracks, int cdTracks) {
    return "Hmm\xE2\x80\xA6 that release has " + std::to_string(releaseTracks) + " tracks, but your CD has " +
           std::to_string(cdTracks) + ".\n\nShould I use it anyway?";
}

namespace {
const struct { Mood mood; const char* text; } kChatter[] = {
    {Mood::Happy, "Liner notes are love letters from the band. I read every one \xE2\x99\xAA"},
    {Mood::Idle,  "MusicBrainz asks for one question a second, so I'm always polite and patient."},
    {Mood::Idle,  "Everything I do goes in my notebook. That's the log, if you'd like a peek."},
    {Mood::Happy, "Once I've met a disc, I remember it. Even without the internet!"},
    {Mood::Idle,  "The old album-info desk closed a while ago\xE2\x80\xA6 so now I'm here (\xE3\x83\xBB\xCF\x89\xE3\x83\xBB)"},
    {Mood::Idle,  "GnuDB only talks to people who leave an e-mail. Their rule, not mine!"},
    {Mood::Busy,  "If WMP already knows a disc, I never scribble over it. Promise."},
};
} // namespace

unsigned chatterCount() { return unsigned(sizeof kChatter / sizeof kChatter[0]); }

Line chatter(unsigned n) {
    const auto& c = kChatter[n % chatterCount()];
    return say(c.mood, c.text);
}

} // namespace noto
} // namespace ln
