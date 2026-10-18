#!/usr/bin/env python3
"""Usage: strayinput.py [DURATION_S] [START_DELAY_S]
Presses Space at random moments for a while, to check that playback ignores stray real input."""
import random, sys, time
from evdev import UInput, ecodes as e

delay = float(sys.argv[2]) if len(sys.argv) > 2 else 0
ui = UInput({e.EV_KEY: [e.KEY_SPACE]}, name="bw-stray")
time.sleep(1 + delay)
end = time.time() + (float(sys.argv[1]) if len(sys.argv) > 1 else 160)
while time.time() < end:
    time.sleep(random.uniform(0.2, 2.0))
    ui.write(e.EV_KEY, e.KEY_SPACE, 1); ui.syn()
    time.sleep(random.uniform(0.02, 0.4))
    ui.write(e.EV_KEY, e.KEY_SPACE, 0); ui.syn()
ui.close()
