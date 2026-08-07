# LC-1X+ MIDI2DMX — deep code & performance review

Scope: full audit of `Source/` at v1.2.0, focused on the code paths that
execute while the plugin is playing. Conducted after the v1.2.0 feature
work (host automation parameters + Host Sync), because both of those
changes moved significant work onto the audio thread for the first time
and invalidated assumptions the older code was relying on.

Everything in sections 1–3 is **fixed** in v1.2.1 and verified by the test
suite. Section 4 is the honest remainder: known issues that were judged
not worth the risk right now, each with the fix recipe so future-you (or
a future model) can pick them up cold.

---

## 1. Crash

### 1.1 Use-after-free: editor paint vs MIDI-triggered Generate

`parseIncomingMidi()` handles a MIDI-learned `Generate` mapping with:

```cpp
*pat = Pattern::chase(pat->numSteps, pat->numSegments, {255,255,255});
```

That runs on the audio thread (host MIDI) or the direct MIDI-input
callback thread, and it *reassigns* the Pattern — destroying the
`vector<vector<RGBColor>>` grid and freeing every inner allocation.

Meanwhile `GridComponent::paint()`, `BarPreviewComponent::paint()`,
`GridComponent::applyColor()` and the mouse handlers all did:

```cpp
auto* pat = proc.currentBank().current();   // no lock
... pat->getColor(s, seg) ...               // iterating freed memory
```

with no lock at all. Trigger the mapped note while the editor is open and
the message thread reads freed memory. This is a live-performance
scenario — MIDI-triggered generate with the window open is precisely what
the feature is for.

The processor header already documented the invariant ("Any UI code that
… MUST hold this lock"); the drawing code simply never honoured it.

**Fixed:** every editor method that dereferences a Pattern now takes
`proc.dataLock`. It's a recursive `CriticalSection`, so the nested calls
(`mouseWheelMove` → `cellAt`) are fine, and consumers only hold it for
microseconds.

---

## 2. Realtime-safety (audio dropouts)

Host Sync means `processBlock` now drives step advances directly, so
everything reachable from `emitDmxDelta`/`computeDmxState` became audio
thread code. Three separate violations of the "no blocking on the audio
thread" rule were live:

### 2.1 Blocking device I/O on the audio thread

`emitDmxDelta` called `directMidiOut_->sendMessageNow(m)` — a CoreMIDI
system call — up to 128 times in a single invocation, from the audio
callback. It also took `midiOutLock_`, which the message thread can hold
across `MidiOutput::openDevice()` (slow, and it enumerates hardware).
Classic priority inversion.

**Fixed:** added `MidiOutSender`, a lock-free `AbstractFifo` of packed
3-byte messages drained by a dedicated `juce::Thread` at high (but
below-audio) priority. The audio thread now only does an atomic index
bump per message. Overflow sets `outputDesynced_`, which forces the next
pass to re-send all 128 channels so the rig can't latch a stale value.
DMX refreshes at ~44 Hz, so the sub-millisecond handoff is invisible.

### 2.2 Heap allocation on the audio thread

`computeDmxState()` allocated, per fixture, per step:

- `std::vector<RGBColor> colors`
- `pat->getStepColors()` (returns by value)
- `applyCrossfade()` (returns a new vector)
- `fixture.mapColorsToDmx()` (returns `vector<pair<int,int>>`)

Four-plus allocations × fixture count × step rate, inside the audio
callback. `malloc` can block on the allocator's internal lock.
`selectPatternWithCrossfade()` allocated too (`crossfadeFrom_` was a
`std::vector`), and that runs from the audio thread via song mode and the
Pattern parameter.

**Fixed:** added allocation-free variants — `Pattern::getStepColorsInto()`
and `FixtureConfig::mapColorsToDmxInto()` — writing into fixed scratch
buffers owned by the processor (`colorBuf_`, `pairBuf_`, `crossfadeFrom_`,
all `kMaxSegments`/`kMaxChannelPairs` sized). The vector-returning APIs
are retained for UI/export/tests, and a test asserts the two produce
byte-identical output so they can't drift apart.

The per-block `juce::MidiBuffer generated` was also a fresh allocation
each block; it's now a pre-sized member (`generatedMidi_`, reserved in
`prepareToPlay`), and `processBlock` copies into the host buffer rather
than `swapWith` so the reservation survives.

**Verified, not asserted:** `Tests/TestMain.cpp` replaces global
`operator new` with a counter, and `RealtimeSafetyTests` measures real
allocations around `processBlock`. The host-sync path is **exactly zero**
across 200 blocks, including across a crossfading pattern change.

### 2.3 Long lock holds from the UI

`segsSlider.onValueChange` held `dataLock` while rebuilding every pattern
in the bank (allocating a fresh grid each). The audio thread blocks on
that same lock in `processBlock`.

**Fixed:** the rebuild happens outside the lock; only the swap is locked.

---

## 3. Correctness and performance

### 3.1 Crossfade consumed by previews

`applyCrossfade()` incremented `crossfadeProgress_` on **every**
`computeDmxState()` call — including previews. Since v1.2.0 routes every
parameter change through a preview, moving any fader during a fade burned
through it in milliseconds. An 8-step fade could complete in under a
frame.

**Fixed:** `computeDmxState(bool advanceFades)`. Only genuine step
advances (MIDI clock, internal timer, host sync) pass `true`. Regression
test added.

### 3.2 Preview flooding the MIDI link

`parameterChanged` → `triggerAsyncUpdate` → `pushPreview` per parameter
change. Under automation that's one full DMX delta per audio block
(~86/s at 512 samples, ×N automated parameters). A dimmer ramp changes
~24 channels per update ≈ 14 kB/s; a 31250-baud MIDI link carries about
3 kB/s. The rig would visibly lag the automation.

**Fixed:** previews coalesce to ~40 Hz (`kPreviewIntervalMs`), with a
trailing call so the final value always lands. The delayed callback holds
a `weak_ptr` to an alive-flag so it can't touch a destroyed processor.

### 3.3 Editor repainting continuously

`timerCallback` unconditionally called `grid.repaint()`, `barPreview
.repaint()`, `songTimeline.repaint()` at 30 Hz. A 64×16 grid is ~1024
`fillRect` + 1024 `drawRect` + text per frame, burned forever even when
idle.

**Fixed:** a `VisualState` snapshot (step, hue, master, blackout, flood +
colour, pattern, fixture, song block) gates the repaint. Direct edits
still repaint at the point of edit, so nothing is missed.

### 3.4 Smaller items

- Host-sync stepping loop had no upper bound; a pathological
  tempo/subdiv could spin the audio thread. Capped at 64 steps/block.
- `setMidiOutputDevice` wrote `prevDmxState_` (a `dataLock`-owned array)
  while holding `midiOutLock_`. Now sets the atomic `outputDesynced_`.

---

## 4. Known remaining issues (not fixed)

Deliberately left alone, in descending order of how much they'd matter.

### 4.1 `dataLock` is still taken on the audio thread

`processBlock` acquires `dataLock` unconditionally. This is architectural
and pre-dates all recent work. It's mitigated (§2.3 removed the worst
holder, and remaining holds are short), but a mutex on the audio thread
is still a mutex.

*Proper fix:* move pattern data behind a lock-free swap — the UI builds a
new immutable snapshot and publishes it with an atomic pointer exchange;
the audio thread reads the current snapshot with no lock at all. Retire
old snapshots on a timer. This is a significant refactor and needs the
test suite green at every step. Worth doing if you ever hear a click that
correlates with editing.

### 4.2 `profileSelector.onChange` still rebuilds patterns under the lock

Same defect §2.3 fixed for `segsSlider`, in the profile-change path.
Rarer in practice (you don't change fixture profiles mid-show), so it was
left rather than risk an untested edit.

*Fix recipe:* copy the `segsSlider.onValueChange` structure exactly —
snapshot the patterns under the lock, rebuild outside it, swap back under
the lock.

### 4.3 MIDI device enumeration every 2 s

The editor timer calls `getAvailableDevices()` for inputs and outputs
every 60 ticks. This can hitch the UI on macOS while Bluetooth MIDI is
scanning.

*Fix recipe:* JUCE 8 has `juce::MidiDeviceListConnection::make()` — a
callback fired on actual device changes. Replaces polling entirely.

### 4.4 `applyHueSat` runs per segment per frame

Full RGB→HSV→RGB with `fmod` for every lit segment. It early-outs when
hue is 0 (the common case), so this only costs anything while hue-shifting.
Fine at current rig sizes; if you ever drive hundreds of channels, a
256-entry hue lookup table would remove it.

### 4.5 The 128-channel DMX ceiling

`dmxState_[128]` and MIDI note numbers cap the plugin at 128 channels at
7-bit resolution. This is inherent to carrying DMX over MIDI notes and is
the real argument for the Art-Net/sACN path discussed separately — a full
512-channel universe at 8 bits, over UDP, no LC-1X hardware required.

---

## Verification

- 27 test groups, 0 failures, against JUCE 8.0.12.
- Zero allocations measured on the host-sync audio path.
- Both plugin formats and the test runner build warning-clean at the
  project's existing warning level.

Not covered by tests, and still worth a human ten minutes: how Host Sync
*feels* against a real Logic session (tempo ramp, loop cycle, locate),
and that the editor still feels responsive now that paint takes the lock.
