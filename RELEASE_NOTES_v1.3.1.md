# LC-1X+ MIDI2DMX v1.3.1

Two bug fixes, both found the first time v1.3.0 met a real rig.

## Fixed: Quick Light fought the plugin for the rig

With Quick Light running in the menu bar, lights set from the plugin came on
and then went out again a few seconds later, over and over.

v1.3.0 gave Quick Light a periodic full re-send of its own state — borrowed
from how real DMX refreshes continuously, so a dropped message heals itself.
That reasoning is sound for a controller that owns the bus, and wrong here: the
converter is shared with the plugin, and Quick Light's "own state" while it is
switched off is every channel at zero. Every few seconds it was quietly
blacking out the plugin's show.

Quick Light is now silent unless it is actually holding the rig. It transmits
when you ask it to — a colour, a brightness, on or off — and when it has to
reopen a connection while lit. Switched off, it sends nothing at all: not on a
timer, not when the MIDI device list changes, not when you pick a different
interface, and not on quit. The automatic reconnection added in 1.3.0 is
unchanged and still watches for the interface coming and going.

Both apps driving the rig at once still means both are talking to it, so if you
turn Quick Light on while the plugin is running they will disagree about the
colour. That part is inherent to two controllers on one universe. The fix is
that an idle Quick Light now stays out of the way instead of asserting
blackout behind your back.

## Fixed: FILL is per fixture, and now behaves like it

FILL holds one fixture on one colour while the others keep their own — that was
always the intent, and setting them worked. Releasing them didn't: switching
back to a fixture and turning FILL off cleared **every** fixture's fill, so you
could build a multi-colour static look but not take it apart one light at a
time.

The FILL button now belongs to the selected fixture:

- It lights up when that fixture is filled, and follows the fixture selector,
  so it shows you the state of the light you're looking at.
- Turning it off releases that fixture and nothing else.
- Turning it back on restores the colour that fixture had before, instead of
  making you find it again.

FLOOD is unchanged and remains the rig-wide override: it is the one that
deliberately reaches across every fixture, and it still wins over any fill.

## Tests

73 cases to 75, covering the two behaviours directly: releasing one fixture's
fill leaves the others lit, and a released fill remembers its colour.
