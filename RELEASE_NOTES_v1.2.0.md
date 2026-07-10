# v1.2.0 — host automation, sample-accurate sync, and a test suite

Three big structural upgrades. Projects saved with v1.0.0 or v1.1.0 load
unchanged.

## New: real DAW automation

The live performance controls are now proper AU/VST3 parameters, visible
and automatable from the host like any synth knob:

- **Master Dimmer** (0–1)
- **Hue Shift** (-180° to +180°)
- **Swing** (50–75%)
- **Blackout** (on/off)
- **Flood** (on/off) and **Flood Colour** (the nine preset colours)
- **Pattern** (1–16, selects within the active fixture's bank)

Draw a dimmer fade across a whole song, automate a white flood hit on the
drop, switch patterns per section from an automation lane — and it replays
identically every night. Everything stays in sync in all directions:
moving a UI slider, playing back automation, or turning a MIDI-learned CC
all update the same parameter, so the host, the editor, and the DMX
output always agree. MIDI-learned Master/Hue/Swing/Blackout controls now
write to the parameters, which means the DAW can *record* your knob
twiddling as automation.

Parameter moves push straight to the hardware even while the transport is
stopped, so you can light the room from an automation lane or the mixer
before the show starts.

## New: Host Sync clock source

A third clock mode alongside Internal and MIDI Clock. Host Sync reads the
DAW playhead position (PPQ) every block and computes step boundaries
sample-accurately:

- Steps land **exactly** on the host grid — no MIDI clock jitter, no
  timer drift.
- Tempo changes, ramps, loops, and locates are followed instantly; a loop
  jump re-locks to the grid instead of stuttering through skipped steps.
- **Swing now works in sync** (it was internal-clock-only before):
  odd steps are delayed by an exact fraction of the step length.
- Transport start/stop and the auto-reset modes behave exactly as they do
  in MIDI Clock mode.

The old removed "Sync DAW" mode value from very early saves maps onto
Host Sync, which is what those sessions wanted anyway. For Logic, Host
Sync is now the recommended mode.

## New: test suite and continuous validation

The engine is now covered by a headless unit test suite (pattern grid,
DMX channel mapping, MIDI clock stepping, Host Sync timing down to the
sample, parameters, state persistence, scenes, crossfade, song mode) that
runs on every push via GitHub Actions, alongside Apple's `auval` and
Tracktion's `pluginval` (strictness 10) validating the built AU and VST3
on macOS. The Windows build remains pinned to the same JUCE release as
macOS. Not a user-facing feature — it's the reason future releases can
move fast without breaking your show.

## Install

1. Download the `.pkg` installer (all three formats) or the individual
   `LC-1X-Plus-MIDI2DMX-v1.2.0.component.zip` below.
2. For the zip: move `LC-1X+ MIDI2DMX.component` to
   `~/Library/Audio/Plug-Ins/Components/`.
3. If macOS Gatekeeper complains:
   `xattr -cr ~/Library/Audio/Plug-Ins/Components/"LC-1X+ MIDI2DMX.component"`
4. Restart Logic / your DAW.

## Upgrading

Drop-in compatible with v1.0.0 and v1.1.0 sessions. Notes:

- Flood on/colour are parameters now, so they restore with the project
  like any other parameter (previously flood was always off after load).
- Host Sync appears as the third entry in the Clock selector; existing
  sessions keep whatever clock mode they had saved.
- If you automated nothing, nothing changes — the parameters simply sit
  at the values your session restores.
