# v1.2.1 — realtime-safety and performance pass

No new features. This release is the result of a full audit of the code
paths that run while the plugin is playing, and it fixes one crash, one
timing bug, and a set of issues that could cause audio dropouts under
load. Sessions from any earlier version load unchanged.

## Fixed: a crash

- **MIDI-triggered Generate could crash the plugin with the editor open.**
  A MIDI-learned Generate mapping rebuilds the current pattern from the
  MIDI thread, freeing the pattern's colour grid — while the editor was
  reading that same grid to draw it. Hitting the mapped note at the wrong
  moment was a use-after-free. All editor drawing and mouse-editing paths
  now take the same lock as the playback threads.

## Fixed: crossfade timing

- **Moving a control mid-fade no longer skips the fade.** Every live
  preview (a fader move, an automation point, a grid edit) recomputed the
  output *and* advanced the crossfade, so touching anything during an
  8-step fade would finish it almost instantly. Fades now advance only on
  real step boundaries, which is what the Fade setting has always meant.

## Fixed: audio dropouts

The clock can now drive DMX from the audio thread (Host Sync, added in
1.2.0), which made several long-standing issues much more likely to bite:

- **MIDI is no longer sent from the audio thread.** Writing to the MIDI
  device is a system call that can block; doing up to 128 of them inside
  the audio callback was the single worst offender. Output now goes
  through a lock-free queue drained by a dedicated high-priority sender
  thread. If that queue ever overflows, the next update re-sends every
  channel so the rig can't be left holding a stale value.
- **The audio thread no longer allocates memory.** Rendering a frame used
  to allocate several times per fixture per step (and once more per
  pattern change for the crossfade). Allocation can block on a lock held
  by another thread, which is exactly how a plugin produces a click. The
  render path now uses fixed buffers throughout. This is enforced by
  tests that count real allocations — the host-sync path is verified at
  exactly zero across 200 blocks.
- **The audio thread no longer waits on the MIDI device lock**, which the
  UI could hold while opening a device (a slow operation).
- **Changing the segment count no longer stalls audio.** Rebuilding every
  pattern in a bank happened with the playback lock held; the rebuild now
  happens up front and only the swap is locked.

## Improved: idle CPU

- **The editor no longer redraws continuously.** The 30 Hz refresh
  repainted the grid, bar preview and song timeline every frame whether
  or not anything had changed — on a large pattern that's around a
  thousand rectangles plus text, thirty times a second, forever. It now
  repaints only when something it draws has actually changed. Expect a
  noticeable drop in idle CPU with the window open, with no change to how
  the playhead animates.
- **Live previews are rate-limited to ~40 Hz.** A smooth automation ramp
  previously produced one full DMX update per audio block (~86/s), each
  re-sending every changed channel — comfortably more than a MIDI cable
  can carry, so the rig would lag behind the automation. Updates are now
  coalesced, with the final value always sent.

## Also

- A pathological tempo/subdivision combination can no longer spin the
  audio thread in the host-sync stepping loop.
- Switching MIDI output device flags a clean resend instead of writing to
  playback state from the wrong thread.

## Tests

The suite grew from 22 to 27 groups. New coverage: allocation counting on
the audio path, crossfade-versus-preview interaction, and equivalence
between the realtime and convenience channel-mapping APIs (so the two can
never silently diverge).
