# v1.0.0 — first stable release

Out of beta. This is the first non-pre-release build of LC-1X+ MIDI2DMX.
Everything in the beta track is in here, plus a round of workflow fixes
and three new features that came out of real-world use since beta.2.

## New features

- **Auto-scroll song timeline**. The song-mode timeline now lives inside
  a horizontally-scrolling viewport. A new **Follow** toggle (on by
  default) keeps the currently playing block in view during song
  playback. Turn it off if you want to scroll or edit the song manually
  while it's running.
- **Custom fixture profiles**. A new **+Profile** / **-Profile** pair
  next to the profile dropdown lets you create and delete your own
  fixture profiles at runtime without touching the source. Profiles are
  stored globally at
  `~/Library/Application Support/AMFAS/LC-1X+ MIDI2DMX/user_profiles.xml`
  and are available across every plugin instance and every session.
  Built-in profiles cannot be deleted.
- **MIDI Clock mode mirrors the DAW tempo**. When Clock Source is set to
  **MIDI Clock**, the BPM slider now dims to 40% and live-mirrors the
  host's current tempo on every timer tick. TAP is disabled in this mode.
  Switching back to Internal restores full control.

## Fixes

- **Par can DMX footprint**. The 76W RGB Par Can profile is now declared
  as a 7-channel fixture (ch1 = dim locked at 100%, ch2/3/4 = R/G/B,
  ch5/6/7 = unused strobe/mode/speed). Placing a second par can now
  correctly skips the full 7-channel block instead of stomping on the
  previous fixture's strobe/mode channels. A new `dmxFootprint()` helper
  on `FixtureConfig` returns the max of `numSegments × channelsPerSegment`
  and the profile's declared total — this is what `addFixture()` and
  `duplicateFixture()` use for auto-placement spacing.
- **Perceptual brightness scaling**. The scroll-wheel brightness
  control on grid cells and the palette **Bright** slider both now
  operate in gamma-corrected perceptual space instead of linear DMX.
  Previously you had to scroll almost to black before the output looked
  any dimmer at all, because LEDs are roughly linear in PWM duty cycle
  but human vision is ~gamma 2.2. Each scroll notch now corresponds to
  a roughly uniform perceived brightness step.
- **Scroll-down floor + visualisation floor**. The scroll-wheel dim
  gesture now floors at a low-but-visible DMX level (≈DMX 15) so you
  can always scroll a cell back up, and the grid paint routine boosts
  any non-black cell's on-screen colour so dim-floor cells are clearly
  visible on the editor canvas while the hardware keeps outputting the
  actual dim value. Fully black cells (erase tool, blackout mode) are
  unaffected.
- **Wheel event accumulator**. Trackpad smooth-scroll and notched mouse
  wheels now feel the same. Wheel deltas are accumulated into a small
  threshold before a brightness step is emitted, so a single aggressive
  trackpad flick can no longer blow a cell straight through the dim
  floor in one event.
- **BPM slider / clock source state machine**. All slider enable/alpha
  state for the BPM and TAP controls is now applied in the clock-source
  combobox's `onChange` callback and the editor constructor — the 30 Hz
  UI timer never touches them. This eliminates the drag-vs-timer race
  that previously caused the slider to feel unresponsive after switching
  clock sources.

## UX polish

- **Song controls row** — button labels and widths reworked so nothing
  truncates at the editor's default width. The new Follow toggle and
  the Song Mode / Block +/- / DupBlk controls all sit on one row with
  room to breathe.
- **Fixture row** — profile selector shrunk to 150px to make room for
  the new +Profile / -Profile buttons next to it. Rename / Export /
  Import button widths tightened so the whole row still fits.
- **New Profile dialog** — a minimal in-editor alert window with a
  name field, a segment-count spinner, and a layout dropdown. New
  profiles persist to the user profiles XML on confirm.
- **Fixture list thread safety revalidated**. All paths that mutate the
  fixture list or the user-profile list hold the processor's
  `dataLock CriticalSection` before touching shared state, consistent
  with the audio-thread consumer paths added in beta.2.

## Install

1. Download `LC-1X-Plus-MIDI2DMX-v1.0.0.component.zip` below.
2. Unzip it.
3. Move `LC-1X+ MIDI2DMX.component` to
   `~/Library/Audio/Plug-Ins/Components/`.
4. If macOS Gatekeeper complains:
   `xattr -cr ~/Library/Audio/Plug-Ins/Components/"LC-1X+ MIDI2DMX.component"`
5. Restart Logic / your DAW.

## Upgrading from beta.2

Drop-in compatible. Existing fixture layouts, patterns, songs, scenes,
MIDI mappings and saved sessions all load without changes. If you had
two par cans placed in beta.2, the second one will load at its original
DMX address; you may want to move it out by a few channels to give the
first par can its full 7-channel footprint — or leave it if you've
verified both work in your current layout.

User profiles are stored in a separate file and are not touched by an
upgrade. Anything you add to `user_profiles.xml` stays between sessions
and between plugin versions.
