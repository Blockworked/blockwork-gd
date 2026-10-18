#!/usr/bin/env python3
"""Launches Geometry Dash (Steam/Proton), opens a level with the selected
Blockwork macro running frame-locked, and reports the outcome from the Geode log.

Usage: autotest.py [--level 10565740] [--macro MACRO_ID] [--timeout 240] [--runs N]
Safe mode in QOL mod is switched on so nothing is saved to the level.
Mods that change physics timing are disabled for the run and restored after.
"""
import argparse, signal, json, os, re, shutil, subprocess, sys, time, glob

GD = os.path.expanduser("~/.local/share/Steam/steamapps/common/Geometry Dash")
SAVE = os.path.expanduser("~/.local/share/Steam/steamapps/compatdata/322170/pfx/drive_c/users/steamuser/AppData/Local/GeometryDash/geode/mods")
APP_ID = "322170"
MOD = "com.blockworked.blockwork"
KEEP = {"geode.loader", "thesillydoggo.qolmod", MOD}
# Mods that alter physics/click timing, or older copies of this mod.
DISABLE = ["syzzi.click_between_frames", "square3ang.cbfextrapolate", "dev.ethanstokes.blockwork",
           "dev.ethanstokes.macros", "treetrain1.macros"]


def jload(p):
    with open(p) as f:
        return json.load(f)


def jsave(p, d):
    with open(p, "w") as f:
        json.dump(d, f, indent=4)


def gd_running():
    return subprocess.run(["pgrep", "-x", "GeometryDash.ex"], capture_output=True).returncode == 0


def kill_gd():
    subprocess.run(["pkill", "-x", "GeometryDash.ex"])
    for _ in range(50):
        if not gd_running():
            return
        time.sleep(0.2)
    subprocess.run(["pkill", "-9", "-x", "GeometryDash.ex"])


def wait_steam_idle():
    # Steam refuses to launch while the previous run's wrapper is still tearing down.
    for _ in range(100):
        if subprocess.run(["pgrep", "-f", f"AppId={APP_ID}"], capture_output=True).returncode != 0:
            return
        time.sleep(0.3)
    subprocess.run(["pkill", "-f", f"AppId={APP_ID}"])
    time.sleep(2)


def newest_log():
    logs = glob.glob(os.path.join(GD, "geode/logs/*.log"))
    return max(logs, key=os.path.getmtime) if logs else None


def run_once(level, timeout, gen_cmd=None):
    wait_steam_idle()
    before = set(glob.glob(os.path.join(GD, "geode/logs/*.log")))
    start = time.time()
    subprocess.Popen(["steam", "-applaunch", APP_ID], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    gen = subprocess.Popen(gen_cmd) if gen_cmd else None
    try:
        return _watch(before, start, timeout, gen_cmd)
    finally:
        if gen:
            gen.terminate()


def _watch(before, start, timeout, gen_cmd=None):
    # Wait for a log created after launch.
    log = None
    while time.time() - start < 120 and log is None:
        time.sleep(1)
        new = set(glob.glob(os.path.join(GD, "geode/logs/*.log"))) - before
        if new:
            log = max(new, key=os.path.getmtime)
    if log is None:
        return "no-log", []
    lines, pos, result = [], 0, "timeout"
    while time.time() - start < timeout:
        time.sleep(1)
        with open(log, errors="replace") as f:
            f.seek(pos)
            chunk = f.read()
            pos = f.tell()
        for line in chunk.splitlines():
            if "[autotest]" in line or "step recording" in line or "[trace-record]" in line or "[trace] " in line or "[trace-key]" in line or "step recording" in line or "[trace-record]" in line or "frame-locked" in line or "blockwork" in line.lower() and ("warn" in line.lower() or "error" in line.lower()):
                lines.append(line)
                print(line, flush=True)
            if "LEVEL COMPLETE" in line:
                result = "complete"
            elif "[autotest] died" in line and not gen_cmd:
                result = "died"
        if result != "timeout":
            break
    return result, lines


def main():
    # Let SIGTERM unwind through the cleanup in `finally`.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(130))
    ap = argparse.ArgumentParser()
    ap.add_argument("--level", type=int, default=10565740)
    ap.add_argument("--local", default="Bloodbath 2", help="name of a local (My Levels) copy to use; empty to download --level")
    ap.add_argument("--no-macro", action="store_true", help="open the level without running the macro")
    ap.add_argument("--all-mods", action="store_true", help="keep the rest of the installed mods enabled")
    ap.add_argument("--offset", type=int, default=None, help="frame-locked step offset (default: the mod's own)")
    ap.add_argument("--speed", type=float, default=1.0, help="QOL mod speedhack value")
    ap.add_argument("--bw-speed", type=float, default=1.0, help="blockwork-gd speed-multiplier setting")
    ap.add_argument("--import-gdr", default=None, help="import this .gdr into Blockwork in-game first (uses it as the macro)")
    ap.add_argument("--import-pending", default=None, help="copy this .gdr into the imports folder and import it in-game")
    ap.add_argument("--ldm", choices=["on", "off"], default=None, help="force low detail mode for the run")
    ap.add_argument("--trace", action="store_true", help="log player position per step")
    ap.add_argument("--queue", action="store_true", help="use the game's input queue")
    ap.add_argument("--cbs", action="store_true", help="enable click between steps")
    ap.add_argument("--macro", default=None)
    ap.add_argument("--record", default=None, metavar="NAME", help="record a new macro NAME from real (virtual-keyboard) input paced from the selected macro, then exit")
    ap.add_argument("--source", default="Bloodbath (frame-locked)", help="macro whose click schedule the virtual keyboard follows when recording")
    ap.add_argument("--fps", type=float, default=None, help="force the game's frame rate (vsync off)")
    ap.add_argument("--use", default=None, metavar="NAME", help="play the macro named NAME (links it to the level)")
    ap.add_argument("--record-speed", type=float, default=0.1, help="game speed while recording")
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--runs", type=int, default=1)
    a = ap.parse_args()

    kill_gd()
    loader_path = os.path.join(SAVE, "geode.loader/saved.json")
    backup = jload(loader_path)
    try:
        loader = dict(backup)
        installed = [os.path.basename(f)[:-len(".geode")] for f in glob.glob(os.path.join(GD, "geode/mods/*.geode"))]
        # Run with only what the test needs: other mods can change physics or UI timing.
        for m in installed if not a.all_mods else DISABLE:
            if m not in KEEP:
                loader[f"should-load-{m}"] = False
        jsave(loader_path, loader)

        qol_path = os.path.join(SAVE, "thesillydoggo.qolmod/saved.json")
        qol = jload(qol_path)
        qol["safe-mode_enabled"] = True
        speed_keys = ["speedhack/enabled_enabled", "speedhack-top_value", "speedhack/music_enabled"]
        qol_speed_backup = {k: qol.get(k) for k in speed_keys}
        if a.record:
            a.speed = a.record_speed
        if a.speed != 1.0:
            qol["speedhack/enabled_enabled"] = True
            qol["speedhack-top_value"] = f"{a.speed:g}"
            qol["speedhack/music_enabled"] = True
        jsave(qol_path, qol)

        os.makedirs(os.path.join(SAVE, MOD), exist_ok=True)
        settings_path = os.path.join(SAVE, MOD, "settings.json")
        settings = jload(settings_path) if os.path.exists(settings_path) else {}
        settings.update({"enabled": not a.no_macro, "action": "start_recording" if a.record else "run_macro", "stop-on-end": True, "frame-locked": True, "trace-steps": a.trace, "queue-inputs": a.queue, "substep-clicks": a.cbs, "speed-multiplier": a.bw_speed, 
                         "blockwork-dir": "Z:\\home\\treetrain1\\.config"})
        if a.offset is not None:
            settings["frame-offset"] = a.offset
        else:
            settings.pop("frame-offset", None)
        jsave(settings_path, settings)

        if a.macro:
            bw = os.path.expanduser("~/.config/Blockwork/settings.json")
            s = jload(bw)
            s["selected_macro_id"] = a.macro
            jsave(bw, s)

        if a.use:
            target = None
            for f in glob.glob(os.path.expanduser("~/.config/Blockwork/macros/*.json")):
                m = jload(f)
                if m.get("name") == a.use:
                    target = m["id"]
            if not target:
                sys.exit(f"no macro named {a.use!r}")
            ms = jload(settings_path)
            ms["_unused"] = None
            ms.pop("_unused")
            saved_path = os.path.join(SAVE, MOD, "saved.json")
            sv = jload(saved_path) if os.path.exists(saved_path) else {}
            lm = sv.get("level-macros", {})
            lm["local:" + a.local if a.local else f"id:{a.level}"] = target
            sv["level-macros"] = lm
            jsave(saved_path, sv)
            bw = os.path.expanduser("~/.config/Blockwork/settings.json")
            s2 = jload(bw)
            s2["selected_macro_id"] = target
            jsave(bw, s2)

        results = []
        for i in range(a.runs):
            req = {"local_name": a.local} if a.local else {"level_id": a.level}
            gen_cmd = None
            if a.record:
                req["new_macro"] = a.record
                srcpath = None
                for f in glob.glob(os.path.expanduser("~/.config/Blockwork/macros/*.json")):
                    m = jload(f)
                    if m.get("name") == a.source:
                        srcpath = f
                    elif m.get("name") == a.record:
                        os.remove(f)  # stale earlier recording
                if not srcpath:
                    sys.exit(f"no source macro named {a.source!r}")
                try:
                    os.remove("/tmp/bw_step.txt")
                except OSError:
                    pass
                gen_cmd = [sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "inputgen.py"), srcpath]
            if a.fps:
                req["fps"] = a.fps
            if a.ldm:
                req["ldm"] = a.ldm == "on"
            if a.import_gdr:
                req["import_gdr"] = "Z:" + os.path.abspath(a.import_gdr).replace("/", "\\")
            if a.import_pending:
                imports = os.path.expanduser("~/.config/Blockwork/imports")
                os.makedirs(imports, exist_ok=True)
                shutil.copy(a.import_pending, imports)
                req["import_pending"] = True
            jsave(os.path.join(SAVE, MOD, "autotest.json"), req)
            print(f"--- run {i + 1}/{a.runs}", flush=True)
            res, _ = run_once(a.level, a.timeout, gen_cmd)
            print("RESULT:", res, flush=True)
            results.append(res)
            kill_gd()
        print("SUMMARY:", results)
        return 0 if all(r == "complete" for r in results) else 1
    finally:
        kill_gd()
        jsave(loader_path, backup)
        try:
            qol = jload(qol_path)
            qol_speed_backup["speedhack/music_enabled"] = True  # user wants speedhack music on
            qol_speed_backup["speedhack/enabled_enabled"] = False
            qol_speed_backup["speedhack-top_value"] = "1"
            for k, v in qol_speed_backup.items():
                if v is None:
                    qol.pop(k, None)
                else:
                    qol[k] = v
            jsave(qol_path, qol)
        except NameError:
            pass


if __name__ == "__main__":
    sys.exit(main())
