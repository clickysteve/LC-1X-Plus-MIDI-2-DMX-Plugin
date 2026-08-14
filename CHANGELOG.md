# Changelog

All notable changes to LC-1X+ MIDI2DMX. Each entry links to the full
release notes for that version.

The version in `CMakeLists.txt` is the single source of truth; `release.sh`
reads it from there.

---

## [1.3.0] — Host Sync fixed, per-fixture FILL, menu bar app

Full notes: [RELEASE_NOTES_v1.3.0.md](RELEASE_NOTES_v1.3.0.md)

**Fixed: Host Sync did nothing in Logic.** It assumed the host would always
report a musical position and had no fallback when it didn't, so it sat
silent while the other two clocks (which need nothing from the host) kept
working. The position is now derived from the timeline and tempo, and then
from the sample position, when PPQ isn't reported; a rolling transport is
inferred from a moving playhead for hosts that never report one; and step
scheduling no longer depends on the audio buffer's shape. A readout in the
transport row now shows what the DAW is actually providing, including the
case the plugin can't fix — the host not rendering it at all.

**Added: FILL.** FLOOD lights the whole rig one colour; FILL does the same
to one fixture, and each fixture keeps its own, so several colours can be
held across the rig at once. UI and MIDI Learn driven, saved with the
project. FLOOD still overrides it, BLACKOUT still kills it. The pattern-
editing button previously called Fill is now Fill Grid.

**Added: LC-1X+ Quick Light**, a menu bar app for turning the rig on
without opening a DAW. Colour, brightness, MIDI output and a rig setup
window; shares the plugin's fixture profiles; blacks out on quit.

**Tests** — 41 cases to 56. The Host Sync regression tests fail against
1.2.1 and pass here, including one that guards against treating the audio
engine's free-running sample clock as a timeline position.

## [1.2.1] — realtime-safety and performance pass

Full notes: [RELEASE_NOTES_v1.2.1.md](RELEASE_NOTES_v1.2.1.md) ·
Audit: [CODE_REVIEW_v1.2.md](CODE_REVIEW_v1.2.md)

No new features. A full audit of the code paths that run while the plugin
is playing.

**Fixed**

- Use-after-free crash: a MIDI-learned Generate rebuilt the pattern from the
  MIDI thread while the editor was drawing it. All editor draw and mouse
  paths now take the playback lock.
- Crossfade was consumed by previews — any fader move during an 8-step fade
  finished it almost instantly. Fades now advance only on real step
  boundaries.
- MIDI is no longer sent from the audio thread. Output goes through a
  lock-free queue drained by a dedicated sender thread; overflow forces a
  full resend so the rig can't latch a stale value.
- The audio thread no longer allocates. Fixed scratch buffers replace the
  per-fixture, per-step vector allocations. Enforced by tests that count
  real allocations (verified at exactly zero across 200 blocks).
- The audio thread no longer waits on the MIDI device lock.
- Changing segment count no longer stalls audio — the pattern rebuild
  happens outside the lock.
- The host-sync stepping loop is bounded, so a pathological tempo/subdivision
  can't spin the audio thread.

**Improved**

- The editor repaints only when something it draws has changed, instead of
  redrawing the whole grid at 30 Hz forever. Noticeably lower idle CPU.
- Live previews coalesce to ~40 Hz, with the final value always sent. An
  automation ramp previously generated more DMX traffic than a MIDI cable
  can carry.

**Tests** — grew from 22 to 27 groups: allocation counting, crossfade vs
preview, and equivalence between the realtime and convenience channel-mapping
APIs.

---

## [1.2.0] — host automation, sample-accurate sync, and a test suite

Full notes: [RELEASE_NOTES_v1.2.0.md](RELEASE_NOTES_v1.2.0.md)

**Added: real DAW automation.** The live performance controls are now proper
AU/VST3 parameters, visible and automatable from the host: Master Dimmer,
Hue Shift, Swing, Blackout, Flood + Flood Colour, and Pattern. Everything
stays in sync in all directions — a UI slider, an automation lane, and a
MIDI-learned CC all drive the same parameter, so the host, the editor and
the DMX output always agree. That also means the DAW can *record* your knob
moves as automation. Parameter moves reach the hardware even with the
transport stopped.

**Added: Host Sync clock source.** A third mode alongside Internal and MIDI
Clock, reading the DAW playhead position (PPQ) each block and computing step
boundaries sample-accurately. No MIDI clock jitter, no timer drift. Tempo
changes, ramps, loops and locates are followed instantly, and a loop jump
re-locks to the grid instead of stuttering. Swing works in sync mode for the
first time. Recommended for Logic.

**Added: test suite and CI.** Headless unit tests covering the pattern grid,
DMX mapping, MIDI clock stepping, Host Sync timing to the sample, parameters,
persistence, scenes, crossfade and song mode — run on every push via GitHub
Actions alongside `auval` and `pluginval` (strictness 10).

**Note:** the GitHub release for 1.2.0 also carries the 1.1.0 work below,
which was never separately tagged.

---

## [1.1.0] — stability and correctness

Full notes: [RELEASE_NOTES_v1.1.0.md](RELEASE_NOTES_v1.1.0.md)

Never tagged as its own release; shipped as part of 1.2.0.

**Fixed: features that silently did nothing**

- Crossfade. The Fade selector was wired to a blend routine that never
  received its "from" colours, so it had no effect at all.
- Brightness CC. A MIDI-learned Brightness CC wrote to a value nothing read.
- Host MIDI output. The plugin declared MIDI output but never wrote to the
  host's buffer — only the direct CoreMIDI connection carried the DMX stream.

**Fixed: crashes and races** — pattern add/duplicate/delete and step-count
changes during playback, MIDI-learn under load, project save during playback,
and MIDI scene load running a full state restore on the audio thread.

**Fixed: correctness** — scene snapshots no longer embed every other scene
(the project file used to grow geometrically), mixed-length patterns wrap per
fixture, pattern import from a larger fixture no longer shears, and the legacy
"Sync DAW" clock value is repaired on load rather than only when the editor
opens.

**Polish** — undoable scroll-wheel dimming, gamma-correct Fill, duplicated
fixtures keep their name, consistent 40–300 BPM range.

---

## [1.0.0] — first stable release

Full notes: [RELEASE_NOTES_v1.0.0.md](RELEASE_NOTES_v1.0.0.md)

**Added** — auto-scrolling song timeline with a Follow toggle; custom fixture
profiles created and deleted at runtime, stored globally in
`~/Library/Application Support/AMFAS/LC-1X+ MIDI2DMX/user_profiles.xml`; BPM
slider mirrors the host tempo in MIDI Clock mode.

**Fixed** — par can DMX footprint (now a declared 7-channel fixture, so a
second par can no longer stomps the first one's channels); perceptual
(gamma 2.2) brightness scaling for the scroll wheel and Bright slider; a
visible dim floor so you can always scroll a cell back up; wheel-event
accumulation so trackpads and notched mice feel the same; BPM/TAP enable
state moved out of the UI timer.

---

## [1.0-beta.2] — par can + MIDI clock fixes

Full notes: [RELEASE_NOTES_v1.0-beta.2.md](RELEASE_NOTES_v1.0-beta.2.md)

Crash on add-segment fixed by serialising all mutating and consuming paths
through a single lock. 76W RGB Par Can remapped to a 4-channel profile with
the dim channel locked at 100%. First step no longer eaten when MIDI clock
starts, via the new Last Step auto-reset mode. DMX start address is 1-based
in the UI. Flood routes each fixture through its own channel layout.

---

## [1.0-beta] — first public beta

Initial release.
