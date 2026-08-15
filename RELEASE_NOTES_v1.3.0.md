# v1.3.0 — Host Sync fixed, per-fixture FILL, show recording, and a menu bar app

Sessions from any earlier version load unchanged.

## Fixed: Host Sync

**Host Sync did nothing in Logic.** The step maths was never the problem —
it's covered by tests that check timing to within two samples — but Host
Sync is the one clock that depends entirely on the host, and it was
written assuming the host would always provide a musical position. When
the host doesn't, the plugin had no fallback and simply sat there. The
other two clocks kept working because neither one needs the host at all,
which made it look like Host Sync specifically was broken.

Three changes, in order of how much they matter:

- **The position is now derived when it isn't reported.** If the host gives
  no musical position but has genuinely answered for its transport state,
  the plugin works the position out from the timeline and the tempo
  instead. That qualifier matters: when a host answers *neither*, the AU
  wrapper hands over the audio engine's own free-running sample counter,
  which climbs forever whether or not anything is playing. The plugin now
  tells the two apart and refuses to sync off the second one.
- **A rolling transport is inferred from a moving playhead.** Hosts that
  never report "playing" left the plugin convinced the transport was
  stopped. If the host has never once claimed to be playing but its
  musical position is advancing over several blocks, the plugin believes
  the position. A frozen playhead, and a nudged one, are both still
  treated as stopped.
- **Step scheduling no longer depends on the audio buffer.** A MIDI effect
  can legitimately be rendered with no audio channels; the block length
  now falls back to the prepared block size.

**And a readout so this is never a mystery again.** In Host Sync mode the
transport row now shows what the DAW is actually handing over: the bar and
beat it thinks you're on, the tempo, and whether it's rolling. If the host
isn't providing a position it says so, and if the host isn't running the
plugin at all it says that instead — which is the single most useful thing
to know, because that one isn't fixable from inside the plugin. On Logic,
a MIDI FX plugin needs to be on a track the host actually renders; if the
readout says the plugin isn't being processed, move it to a Software
Instrument track's MIDI FX slot.

## New: MIDI to Host toggle (and why Host Sync needs it)

Logic only runs a MIDI FX plugin on a track it actually renders, which means
a Software Instrument track **with an instrument loaded**. An empty one is
never processed at all, which is the single most likely reason Host Sync
appears dead — the new readout says so explicitly.

But loading an instrument used to have an unpleasant side effect: the plugin
replaces the track's MIDI output with the DMX note stream, so the instrument
received up to 128 notes at once and made a racket.

There's now a **To host** toggle next to the MIDI Out selector, **off by
default**. With it off the DMX stream goes only to the MIDI output device,
which is what drives your rig anyway, and the instrument sitting after the
plugin hears nothing. Turn it on in hosts where a MIDI effect's output can be
routed to a MIDI port — Ableton, Bitwig, Reaper — which is the case the
feature exists for. It's saved with the project.

If you're on Logic: load any instrument, leave **To host** off, and set Clock
to Host Sync.

## New: FILL — per-fixture colour override

FLOOD lights the whole rig one colour. **FILL** does the same thing to one
fixture at a time, and every fixture remembers its own, so you can hold the
left bar on red and the right bar on blue at once.

It works exactly like FLOOD: arm the FILL button, then click a colour and
that colour is held on the *selected* fixture. Select another fixture, pick
another colour, and both stay lit. Clicking the same colour again releases
it, and turning FILL off releases every fixture. FLOOD still overrides
everything while it's up, and BLACKOUT still kills the lot.

Fills output with the transport stopped and are saved with the project.

FILL is driven from the UI and MIDI Learn rather than being a host
parameter. A parameter carries one value per plugin instance, but a fill is
per fixture, so "the fill colour" would have to mean "the selected
fixture's fill colour" — and then merely changing which fixture you're
looking at would read to the DAW as an automation move. FLOOD remains
automatable and is the right tool for that job.

The old **Fill** button (which paints the pattern grid) is now labelled
**Fill Grid**, because having two unrelated things called Fill in the same
row was confusing. It does exactly what it did before.

## New: record the light show as MIDI

**REC**, next to the MIDI Out selector, captures what the plugin actually
sends — step advances, flood hits, fills, scene recalls, fader moves, the lot
— stamped against the musical clock. Stopping offers to save a `.mid` file.
Drop that onto an External MIDI track pointed at your LC-1X+ and the show
plays back from the timeline with the plugin doing nothing, and because it's
ordinary MIDI you can then nudge, trim or redraw any cue in the DAW.

It records the performance, not the sequence. Everything you did by hand
lands in the file exactly where you did it, which is the part that rendering
the pattern offline could never reproduce.

Details worth knowing:

- A take always starts at zero, so it can be dropped anywhere on the timeline
  regardless of where you were when you armed it.
- The recording opens with the rig's current state, not just what changes
  afterwards, so anything already lit is in the file.
- Stopping and restarting the transport mid-take doesn't break it. Every
  source of musical position rewinds at some point — the MIDI clock counter
  is zeroed on transport start, the playhead rewinds on a loop — and the take
  carries on forwards regardless.
- Position resolution follows the clock: sample-accurate on Host Sync, about
  20 ms on MIDI Clock, and on the internal clock the gap since the last step
  is measured so gestures between steps keep their timing.
- The capture buffer holds about 65,000 events, which is several minutes of a
  busy show. If it ever fills, the readout says so plainly and saving warns
  you, rather than letting you discover a silently incomplete take later.

Capture is realtime-safe: the audio thread only writes into a lock-free
queue, verified by a test that measures zero allocations across 200 blocks
while recording.

## New: LC-1X+ Quick Light

A small menu bar app for the case the plugin was never meant to cover:
walking into the studio and wanting the lights on without opening a DAW.

Click the menu bar icon, pick a colour, done. There's a brightness
submenu, a MIDI output picker, and a rig setup window where you describe
your fixtures the same way you would in the plugin — profile, DMX start
address, segment count. It shares the plugin's fixture profiles, including
any custom ones you've made, so a rig described in one addresses the same
channels in the other.

The icon shows a filled dot in the current colour when the rig is lit and a
hollow ring when it isn't, so you can tell at a glance. It runs as a menu
bar agent with no Dock icon, it never turns the lights on by itself at
launch, and it blacks the rig out when you quit rather than leaving it lit
by a process that no longer exists.

It's installed to `/Applications` by the installer alongside the standalone.

## Tests

The suite grew from 41 cases to 67. New coverage: Host Sync against hosts
that report no musical position, no transport state, a free-running engine
clock, and a zero-length audio buffer; the diagnostics themselves; and
FILL's interaction with FLOOD, BLACKOUT, a stopped transport, multiple
fixtures and state persistence; and, for capture, MIDI-file round-tripping,
FIFO overflow, position sources per clock, opening state, surviving a
transport restart, and allocation counting while armed.

Two of those are there specifically because the first attempt at this fix
was wrong. Deriving a position from the timeline looked like a safe
fallback until it turned out that JUCE's AU wrapper reports the render
engine's free-running sample counter when the host doesn't answer the
transport callback — which would have left the rig free-running with no way
to stop it from the DAW. That is a worse failure than the one being fixed,
so there is now a test that fails if the plugin ever treats that counter as
a timeline again.

The Host Sync regression tests fail against 1.2.1 and pass against this
build, which is the specific evidence that the bug is fixed rather than
merely different.
