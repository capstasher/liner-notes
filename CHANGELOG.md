# Changelog

Notable changes to LinerNotes. Versions follow [Semantic Versioning](https://semver.org).

## [1.0.1] - 2026-10-17

### Fixed
- **WMP froze when a CD went in**, for up to a minute on the first disc and sluggishly
  after that. LinerNotes rewrote every title and album detail each time, even when WMP
  already had them right, and every write makes WMP update its library (and any copies
  you've ripped). It now compares first and only writes what's different, so a disc WMP
  remembers costs nothing.
- A disc she's met before no longer opens the drive at all: its table of contents is
  remembered under WMP's own copy of it. Opening a busy drive could take many seconds.
- Mixed-mode CDs (game discs such as Half-Life, with the game's data on track 1 and the
  soundtrack after it) got every title shifted down by one: the first audio track was
  called "[data track]" and the last title went missing. MusicBrainz and GnuDB both count
  the data track, but WMP only lists the audio tracks, so LinerNotes now uses the disc's
  table of contents to leave data tracks out before matching titles to WMP's tracks.
- Titles are matched to WMP's tracks by position whenever the counts agree, instead of by
  WMP's own track numbers.
- GnuDB entries for Enhanced CDs and mixed-mode discs are no longer thrown away for having
  one track too many.
- Searching from the window now finds releases whose disc lists a data track, and their
  tracklists are trimmed the same way.
- Track lengths filled in from the disc no longer count the gap before an Enhanced CD's
  data session as part of its last song.

### Added
- The log notes anything that holds WMP up after a disc goes in: how long WMP stops
  responding and how much of that was LinerNotes, slow steps on WMP's thread, how long
  reading the drive took, and how long her window stayed open. Silent when all is well.
- The log's "Applied" line says how many values were already right.

### Repairs
- Discs saved by 1.0.0 with the data track in are corrected when loaded from the cache.
- If WMP still shows "[data track]" from 1.0.0, LinerNotes replaces the shifted titles
  even when *Overwrite titles WMP already has* is off.

## [1.0.0] - 2026-10-13

First public release: the 0.2 lookup engine, now with a host.

### Added
- **Nōto**, the LinerNotes mascot. She sits in the corner of the window and her face
  follows what's happening: waiting, asking MusicBrainz, found it, couldn't find it.
  Click her for a chat. She's drawn in code in outlined, cel-shaded anime style, with a
  small brush engine (`art/brush.py`) for tapered lines; the SVGs open as named layers
  in Inkscape or Krita.
- Her voice for everything the window says. The log stays plain for bug reports.
- An icon and version information on the DLLs.
- WMP's plug-in list introduces her.
- Tests for every line she can say: valid UTF-8, length, mood, and glyph coverage on
  stock Windows fonts.
- Reproducible builds: no link timestamp, so a release can be rebuilt and checked against
  its published SHA-256 sums.
- A CI build on every push.

### Changed
- Window labels reworded in her voice ("What I found for this disc", "Open my notebook (log)").
- The window is 44 px taller to make room for her and her speech bubble.

## [0.2.1] - 2026-10-12

### Removed
- Cover art, entirely: no Cover Art Archive requests, art cache, folder writes, cover
  panel, or rip and library event hooks.

## [0.2.0] - 2026-09-21

### Added
- The year is written to `ReleaseDate` (WMP ignores `WM/Year`, and `ReleaseDateYear` is
  read-only), with read-back verification. The album's original release year is preferred.
- Album details are also written to the CD playlist itself.
- The CD playlist's attributes are included in the diagnostic dump.

## [0.1.0] - 2026-09-21

### Added
- First version: table of contents read from the drive; MusicBrainz disc ID, fuzzy TOC and
  CD-stub lookup; optional GnuDB; candidate ranking; a picker with manual search; a per-disc
  cache for offline replay; re-applying titles when WMP's own dead lookup reverts them.

[1.0.1]: https://github.com/capstasher/liner-notes/releases/tag/v1.0.1
[1.0.0]: https://github.com/capstasher/liner-notes/releases/tag/v1.0.0
