# v1.0-beta.2 — par can + MIDI clock fixes

Second beta. Focus: the 76W RGB par can now actually works, the "first
step gets eaten when MIDI clock starts" bug is gone, and adding segments
no longer crashes the plugin.

## Fixes

- **Crash on add-segment**. A data race between the UI thread and the
  audio/timer threads could crash the plugin when adding a segment to a
  fixture. All mutating paths (segment count, profile change, undo/redo,
  deserialise, fill/generate, etc.) and consumer paths (timer callback,
  MIDI clock advance, host-stop reset) are now serialised through a
  single `CriticalSection`.
- **76W RGB Par Can mapping**. Reworked to a single-segment 4-channel
  profile (ch1 = dim, ch2/3/4 = R/G/B). The dim channel is now locked
  at 100% and brightness is driven by the RGB channels directly, so
  flooding red actually turns the lamp red.
- **First step skipped when MIDI clock starts**. Fixed via the new
  Last Step auto-reset mode (see below). The first MIDI clock tick
  after Start now wraps cleanly back to step 0 instead of landing on
  step 1.
- **DMX start address is now 1-based in the UI**. The plugin internally
  still uses 0-based offsets, but the slider reads 1..121 to match the
  values printed on physical fixtures.
- **Flood across mixed fixtures**. Flood All now routes every fixture
  through its own channel layout, so a rig with a Giga 8 Bar *and* a
  par can will flood both correctly on their respective DMX channels.
  (Previously a fast path bypassed the fixture profile for 3ch/no-dim
  layouts.)

## Changes

- **Auto-reset is now a dropdown**: Off / 1st Step / Last Step.
  - *Off* — stay where you are on stop.
  - *1st Step* — rewind to step 1 on stop.
  - *Last Step* — park at the last step so the first MIDI clock tick
    after the next Start wraps back to step 0. **Recommended** when
    using MIDI clock input.
- **New defaults for fresh plugin instances**:
  - Clock source → **MIDI Clock** (was Internal)
  - Auto-reset → **Last Step** (was Off)
- **Fixture profile list trimmed** to the two profiles actually in
  use: `Giga 8 Bar (24ch)` and `76W RGB Par Can (4ch)`. The old
  `Dimmer+RGB (4ch)` and `RGBW (4ch per segment)` generic profiles
  have been removed. Old session files that referenced them will
  fall back safely to the first profile on load.

## Docs

- README format line corrected — this plugin is distributed as an
  Audio Unit MIDI FX Component (`.component`) only.
- Added an up-top disclaimer: unofficial plugin, not affiliated with,
  endorsed by, or supported by BoomLights.

## Install

1. Download `LC-1X-Plus-MIDI2DMX-v1.0-beta.2.component.zip` below.
2. Unzip it.
3. Move `LC-1X+ MIDI2DMX.component` to
   `~/Library/Audio/Plug-Ins/Components/`.
4. If macOS Gatekeeper complains:
   `xattr -cr ~/Library/Audio/Plug-Ins/Components/"LC-1X+ MIDI2DMX.component"`
5. Restart Logic / your DAW.

## Known items for next beta

- Auto-scroll the pattern selector to the currently playing pattern
  during song mode playback.
