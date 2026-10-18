#!/usr/bin/env python3
"""Test input source: presses Space through a virtual keyboard (real OS input)
following a macro's click schedule, paced by the game's own step counter.

The mod (in autotest + step-recording mode) writes "<step> 1" to
/tmp/bw_step.txt before every physics step. Each click is sent one step ahead of
the step it should land on, so the game picks it up on exactly that step even
at slowed game speed. If the step counter goes backwards (the attempt
restarted) the schedule starts over.

Usage: inputgen.py MACRO_JSON [--offset -1] [--stop-file PATH]
"""
import argparse, json, math, os, sys, time
import evdev
from evdev import UInput, ecodes as e

STEP_MS = 1000.0 / 240.0


def schedule(path, offset):
    ins = json.load(open(path))["strands"][0]["instructions"]
    t, out = 0.0, []
    for i in ins:
        k = i["kind"]
        if isinstance(k, dict) and "Wait" in k:
            t += k["Wait"]["value"]
        elif isinstance(k, dict) and "Token" in k and "Button" in k["Token"]:
            _, d = k["Token"]["Button"]
            # Playback lands an input whose time is T on the first step s with
            # (s - offset) * STEP_MS >= T.
            # Same 1e-6 ms slack the engine's lockstep clock uses.
            step = math.ceil((t - 1e-6) / STEP_MS) + offset
            out.append((step, d == "Press"))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("macro")
    ap.add_argument("--offset", type=int, default=-1)
    ap.add_argument("--file", default="/tmp/bw_step.txt")
    ap.add_argument("--stop-file", default=None)
    a = ap.parse_args()
    sched = schedule(a.macro, a.offset)
    print(f"inputgen: {len(sched)} inputs, last at step {sched[-1][0]}", flush=True)
    ui = UInput({e.EV_KEY: [e.KEY_SPACE]}, name="bw-inputgen")
    time.sleep(1)
    idx, last, held = 0, -1, False
    try:
        while not (a.stop_file and os.path.exists(a.stop_file)):
            try:
                with open(a.file) as f:
                    parts = f.read().split()
                step = int(parts[0])
            except (OSError, ValueError, IndexError):
                time.sleep(0.0005)
                continue
            if step < last:
                idx = 0
                if held:
                    ui.write(e.EV_KEY, e.KEY_SPACE, 0); ui.syn(); held = False
                print(f"inputgen: attempt restarted (step {step})", flush=True)
            last = step
            # Send as soon as the previous step has begun.
            while idx < len(sched) and step >= sched[idx][0] - 1:
                s, down = sched[idx]
                if step > s:
                    print(f"inputgen: late input {idx} (wanted {s}, at {step})", flush=True)
                if os.environ.get("INPUTGEN_DEBUG"):
                    print(f"inputgen-sent idx={idx} want={s} seen={step}", flush=True)
                ui.write(e.EV_KEY, e.KEY_SPACE, 1 if down else 0)
                ui.syn()
                held = down
                idx += 1
            time.sleep(0.0005)
    finally:
        ui.close()


if __name__ == "__main__":
    sys.exit(main())
