# Changelog

Notable changes to LinerNotes. Versions follow [Semantic Versioning](https://semver.org).

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

[1.0.0]: https://github.com/capstasher/liner-notes/releases/tag/v1.0.0
