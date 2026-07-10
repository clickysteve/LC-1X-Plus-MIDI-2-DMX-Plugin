# v1.1.0 — stability and correctness release

A full-codebase review pass. No new panels or workflows — this release
makes the features that were already in the UI actually work, and fixes
a family of threading bugs that could crash the plugin mid-show. Saved
sessions from v1.0.0 load without changes.

## Fixed: features that silently did nothing

- **Crossfade works now.** The **Fade** selector (0/2/4/8/16 steps) was
  wired to a blend routine that never received its "from" colours, so it
  had no effect at all. Pattern changes — from the pattern dropdown, the
  1–9 keys, a MIDI PatternSelect mapping, or a song-mode block change —
  now capture the outgoing pattern's colours and blend into the new
  pattern over the selected number of steps. The crossfade applies to
  the active fixture. The Fade selector also now shows the saved value
  when you reopen a session instead of always showing 0.
- **Brightness CC works now.** A MIDI-learned Brightness CC previously
  wrote to a value nothing read. It now drives the palette **Bright**
  slider (and therefore paint brightness) in both directions — move the
  slider and the CC value follows; move the CC and the slider follows.
- **Host MIDI output works now.** The plugin declared MIDI output but
  never wrote anything to the host's MIDI buffer — only the direct
  CoreMIDI device connection carried the DMX note stream. Step advances
  and transport resets driven by MIDI clock now also emit into the
  host's MIDI FX chain, so you can route to the LC-1X+ through your
  DAW's own MIDI routing instead of (or as well as) the direct device.

## Fixed: crashes and races

- **Pattern edits during playback are safe.** Adding, duplicating, or
  deleting a pattern, and changing a pattern's step count, while the
  clock was running could crash the plugin — the audio/clock threads
  hold a pointer to the current pattern, and these edits could reallocate
  it out from under them. All of these paths now take the same data lock
  as the playback threads. Song-block edits (+Block / -Block / DupBlk /
  repeat +/-) got the same treatment.
- **MIDI-learn is safe under load.** Learning a new mapping while MIDI
  was arriving on both the host track and a direct MIDI input could
  corrupt the mapping list (two threads mutating/iterating it at once).
  All mapping reads and writes are now serialised.
- **Project save is safe during playback.** Host autosave could
  serialise the plugin state while a clock thread was mutating patterns.
  State writes now hold the data lock.
- **Scene load via MIDI no longer runs on the audio thread.** A
  MIDI-mapped SceneLoad used to execute a full state restore — XML
  parse, allocation, even MIDI device opening — directly on the
  audio/MIDI thread, with a plausible deadlock against the MIDI input
  callback. The load is now deferred to the message thread, and the
  editor refreshes its controls when it happens.
- **Assorted smaller races** — BPM, active fixture, song-mode flag,
  crossfade steps and the MIDI clock counter are now atomics; the
  internal-clock timer no longer reads the current pattern outside the
  lock; state restore no longer holds the data lock while re-opening
  MIDI devices.

## Fixed: correctness

- **Scene snapshots no longer balloon the project file.** Storing a
  scene used to embed every *other* scene's data inside it, so repeated
  scene stores grew the saved state geometrically. Scene snapshots now
  exclude the scenes table. Existing saved scenes still load.
- **Mixed pattern lengths wrap correctly.** A fixture whose pattern is
  shorter than the active fixture's used to go dark for the tail of
  every cycle; it now wraps around its own pattern length.
- **Pattern import from a bigger fixture aligns correctly.** Importing a
  pattern JSON built for a fixture with more segments used to shear the
  colours diagonally across the grid; the extra segments are now dropped
  cleanly.
- **Headless sessions can't get stuck in a dead clock mode.** The legacy
  "Sync DAW" clock-source value is now repaired on state load, not just
  when the editor opens — a restored session on a live rig no longer
  sits silently with no clock at all until you open the plugin window.
- **Corrupt-state hardening.** A negative saved fixture index is clamped
  instead of indexing out of bounds.

## UX polish

- Scroll-wheel dimming is now undoable (one undo snapshot per scroll
  gesture, so Cmd-Z behaves like it does for painting).
- **Fill** now applies the same perceptual (gamma 2.2) brightness curve
  as painting, so filled cells match painted cells at the same Bright
  slider position.
- Duplicating a fixture keeps its name ("Left Bar copy" instead of
  "Fixture 3"). Auto-numbered fixtures still renumber as before.
- BPM range is now 40–300 everywhere (slider, tap tempo, and BPM CC
  previously disagreed about the lower bound).
- Matrix Rain generates a different pattern each time, like the other
  random generators.
- The Swing slider's tooltip now notes that swing applies to the
  internal clock only — in MIDI Clock mode the plugin follows the
  incoming ticks as-is.

## Build / release

- Windows CI now builds against a pinned JUCE release (8.0.12, matching
  the macOS build) instead of whatever JUCE master happens to be that
  day.
- `release.sh` now fails immediately with a clear message if Apple's
  notary service rejects a submission, instead of failing later at the
  stapling step.

## Install

1. Download `LC-1X-Plus-MIDI2DMX-v1.1.0.component.zip` below (or the
   `.pkg` installer for all three formats).
2. Unzip it.
3. Move `LC-1X+ MIDI2DMX.component` to
   `~/Library/Audio/Plug-Ins/Components/`.
4. If macOS Gatekeeper complains:
   `xattr -cr ~/Library/Audio/Plug-Ins/Components/"LC-1X+ MIDI2DMX.component"`
5. Restart Logic / your DAW.

## Upgrading from v1.0.0

Drop-in compatible. Fixture layouts, patterns, songs, MIDI mappings and
saved sessions all load unchanged. Two behaviour changes to be aware of:

- The plugin now emits its DMX note stream to the host's MIDI output as
  well as the direct device. If you have a software instrument sitting
  after the plugin in a MIDI FX chain, it will start receiving notes —
  move the plugin to its own track if that's not what you want.
- Scenes stored with v1.1.0 are much smaller than before. Scenes stored
  with v1.0.0 still load, and re-storing them once in v1.1.0 shrinks
  them permanently.
