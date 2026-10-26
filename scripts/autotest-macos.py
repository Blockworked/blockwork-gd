#!/usr/bin/env python3
"""Run native macOS GD attempts with a fresh GDR import for every attempt.

Preserves game saves and restores mod/Blockwork settings after stopping the
owned game process. Results and exact configuration backups remain in --output.
"""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
import uuid

MOD = 'com.blockworked.blockwork'


def load(path):
    return json.loads(path.read_text()) if path.exists() else {}


def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.autotest-tmp')
    temporary.write_text(json.dumps(data, indent=2))
    temporary.replace(path)


def game_pids(executable):
    rows = subprocess.check_output(['ps', '-axo', 'pid=,comm='], text=True).splitlines()
    return [int(parts[0]) for row in rows if len(parts := row.strip().split(None, 1)) == 2 and parts[1] == str(executable)]


def stop_owned_game(executable):
    pids = game_pids(executable)
    for pid in pids:
        # Geode handles SIGTERM as a crash and opens crash recovery on the
        # next launch. SIGINT stops our owned test process without fabricating
        # a crash report; real spontaneous crashes remain untouched.
        try:
            os.kill(pid, signal.SIGINT)
        except ProcessLookupError:
            pass
    deadline = time.monotonic() + 5
    while game_pids(executable) and time.monotonic() < deadline:
        time.sleep(.1)
    for pid in game_pids(executable):
        if pid in pids:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    deadline = time.monotonic() + 5
    while any(pid in pids for pid in game_pids(executable)):
        if time.monotonic() >= deadline:
            raise RuntimeError("Owned game process did not stop; configuration restoration is unsafe.")
        time.sleep(.1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=Path.home() / 'Library/Application Support/Steam/steamapps/common/Geometry Dash/Geometry Dash.app')
    parser.add_argument('--level', default='Bloodbath Infinity 2')
    parser.add_argument('--gdr', type=Path, default=Path.home() / 'Downloads/ChesszDC-Bloodbath-Infinity-zBot.gdr')
    parser.add_argument('--speed', type=float, default=1)
    parser.add_argument('--runs', type=int, default=1)
    parser.add_argument('--timeout', type=float, default=300)
    parser.add_argument('--offset', type=int, default=-1)
    parser.add_argument('--queue', action='store_true')
    parser.add_argument('--cbs', action='store_true')
    parser.add_argument('--trace', action='store_true')
    parser.add_argument('--recording-probe', action='store_true', help='verify step recording separately from full-level playback')
    parser.add_argument('--keep-profile', action='store_true', help='preserve other enabled mods and QOL safe-mode/music preferences')
    parser.add_argument('--stray-queue', action='store_true', help='inject and verify rejection of a foreign queued press')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('This runner requires native macOS.')
    if not args.gdr.is_file() or not 0 < args.speed <= 10 or args.runs < 1:
        parser.error('Provide an existing GDR, speed in (0,10], and at least one run.')
    if args.stray_queue and not args.queue:
        parser.error('--stray-queue requires --queue.')
    if args.recording_probe and args.stray_queue:
        parser.error('--stray-queue is a playback test; use it separately from --recording-probe.')
    executable = args.game / 'Contents/MacOS/Geometry Dash'
    if game_pids(executable):
        parser.error('Geometry Dash is already running; close it before starting this isolated test.')
    for row in subprocess.check_output(['ps', '-axo', 'comm='], text=True).splitlines():
        if row.strip().endswith('/blockwork-daemon'):
            parser.error('Close the Blockwork daemon so the game owns its configuration during this test.')
    geode = shutil.which('geode') or str(Path.home() / '.local/bin/geode')
    profiles = subprocess.check_output([geode, 'profile', 'list'], text=True)
    if not any(line.startswith('* ') and str(args.game) in line for line in profiles.splitlines()):
        parser.error('--game must match the active Geode profile.')
    args.output.mkdir(parents=True, exist_ok=False)
    base = Path.home() / 'Library/Application Support/GeometryDash/geode/mods'
    bw = Path.home() / 'Library/Application Support/Blockwork'
    files = [base / 'geode.loader/saved.json', base / 'thesillydoggo.qolmod/saved.json',
             base / MOD / 'settings.json', base / MOD / 'saved.json', bw / 'settings.json',
             base / MOD / 'autotest.json', base / MOD / 'autotest-result.json',
             base / MOD / 'autotest-progress.json']
    backups = {path: path.read_bytes() if path.exists() else None for path in files}
    for name in ['CCGameManager.dat', 'CCLocalLevels.dat']:
        path = base.parents[1] / name
        backups[path] = path.read_bytes()
    backup_dir = args.output / 'settings-before'
    backup_dir.mkdir()
    for index, (path, data) in enumerate(backups.items()):
        if data is not None:
            (backup_dir / f'{index}-{path.parent.name}-{path.name}').write_bytes(data)
    for name in ['CCGameManager.dat', 'CCLocalLevels.dat']:
        shutil.copy2(base.parents[1] / name, backup_dir / name)
    manifest = {'gdr': str(args.gdr.resolve()), 'gdr_sha256': hashlib.sha256(args.gdr.read_bytes()).hexdigest(),
                'level': args.level, 'qol_speed': args.speed, 'frame_offset': args.offset,
                'queue': args.queue, 'cbs': args.cbs, 'stray_queue_probe': args.stray_queue, 'recording_probe': args.recording_probe, 'keep_profile': args.keep_profile, 'runs': [],
                'macos': platform.mac_ver()[0], 'architecture': platform.machine(),
                'shutdown_signal': 'SIGINT',
                'mod_package_sha256': hashlib.sha256((args.game / 'Contents/geode/mods' / (MOD + '.geode')).read_bytes()).hexdigest()}
    save(args.output / 'manifest.json', manifest)
    steam = Path.home() / 'Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/steam_osx'
    launched = False
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(130))
    try:
        loader = load(files[0])
        packages = list((args.game / 'Contents/geode/mods').glob('*.geode'))
        if not args.keep_profile:
            for package in packages:
                loader['should-load-' + package.stem] = package.stem in {'geode.loader', 'thesillydoggo.qolmod', MOD}
            save(files[0], loader)
        manifest['enabled_mods'] = sorted(package.stem for package in packages
                                          if loader.get('should-load-' + package.stem, True))
        qol = load(files[1])
        if not args.keep_profile:
            qol.update({'safe-mode_enabled': True, 'speedhack/music_enabled': True})
        qol.update({'noclip_enabled': False, 'speedhack/enabled_enabled': args.speed != 1,
                    'speedhack-top_value': f'{args.speed:g}'})
        manifest['safe_mode_enabled'] = qol.get('safe-mode_enabled', False)
        manifest['speedhack_music_enabled'] = qol.get('speedhack/music_enabled')
        save(args.output / 'manifest.json', manifest)
        save(files[1], qol)
        settings = load(files[2])
        settings.update({'enabled': True, 'action': 'start_recording' if args.recording_probe else 'run_macro', 'stop-on-end': True,
                         'frame-locked': True, 'frame-offset': args.offset, 'speed-multiplier': 1,
                         'queue-inputs': args.queue, 'substep-clicks': args.cbs,
                         'trace-steps': args.trace, 'blockwork-dir': ''})
        save(files[2], settings)
        seen_macros = set(path.stem for path in (bw / 'macros').glob('*.json'))
        for index in range(args.runs):
            run_id = uuid.uuid4().hex
            request = {'run_id': run_id, 'local_name': args.level, 'import_gdr': str(args.gdr.resolve()), 'stray_queue_probe': args.stray_queue, 'recording_probe': args.recording_probe}
            save(files[5], request)
            files[6].unlink(missing_ok=True)
            files[7].unlink(missing_ok=True)
            before_logs = set((args.game / 'Contents/geode/logs').glob('*.log'))
            crash_dir = args.game / 'Contents/geode/crashlogs'
            before_crashes = set(crash_dir.glob('*.log'))
            launched = True
            launcher_log = (args.output / f'run-{index + 1}-launcher.log').open('w')
            launcher = subprocess.Popen([str(steam), '-applaunch', '322170'], stdout=launcher_log, stderr=subprocess.STDOUT)
            start = time.monotonic()
            result = None
            game_seen = False
            while time.monotonic() - start < args.timeout:
                if files[6].exists():
                    try:
                        candidate = load(files[6])
                        if candidate.get('run_id') == run_id:
                            result = candidate
                            break
                    except json.JSONDecodeError:
                        pass
                running = bool(game_pids(executable))
                game_seen = game_seen or running
                if not running and (game_seen or time.monotonic() - start > 45):
                    result = {'run_id': run_id, 'outcome': 'game-exited' if game_seen else 'launch-timeout', 'launcher_exit': launcher.poll()}
                    break
                time.sleep(.2)
            launcher_log.close()
            if result is None:
                result = {'run_id': run_id, 'outcome': 'timeout'}
            try:
                progress = load(files[7])
                if progress.get('run_id') == run_id:
                    result['last_progress'] = progress
                    if not result.get('macro_id'):
                        result['macro_id'] = progress.get('macro_id', '')
                        result['fresh_import'] = progress.get('fresh_import', False)
            except json.JSONDecodeError:
                pass
            result['wall_seconds'] = time.monotonic() - start
            macro_id = result.get('macro_id', '')
            result['verified_fresh_macro'] = bool(result.get('fresh_import') and macro_id not in seen_macros
                                                  and (bw / 'macros' / f'{macro_id}.json').exists())
            if result['verified_fresh_macro']:
                shutil.copy2(bw / 'macros' / f'{macro_id}.json', args.output / f'run-{index + 1}-macro.json')
                seen_macros.add(macro_id)
            result['passed'] = (result.get('outcome') == 'complete' and result['verified_fresh_macro']
                                and result.get('selected_macro_id') == macro_id
                                and result.get('level') == args.level and result.get('player_dead') is False
                                and (not args.stray_queue or (result.get('stray_queue_injected') and result.get('rejected_foreign_queue_inputs', 0) >= 1)))
            if args.recording_probe:
                expected = [{'step': step, 'secondary': secondary, 'down': down}
                            for step, secondary, down in [(1, False, True), (2, False, False), (3, True, True), (4, True, False)]]
                saved_events = []
                elapsed_ms = 0
                if result['verified_fresh_macro']:
                    macro = load(args.output / f'run-{index + 1}-macro.json')
                    for instruction in macro['strands'][0]['instructions']:
                        kind = instruction['kind']
                        if isinstance(kind, dict) and 'Wait' in kind:
                            elapsed_ms += kind['Wait']['value']
                        elif isinstance(kind, dict) and 'Token' in kind and 'Button' in kind['Token']:
                            button, direction = kind['Token']['Button']
                            saved_events.append({'step': round(elapsed_ms * 240 / 1000 + args.offset),
                                                 'secondary': button == 'Right', 'down': direction == 'Press'})
                result['saved_recorded_inputs'] = saved_events
                result['passed'] = (result.get('outcome') == 'recording-probe-complete'
                                    and result['verified_fresh_macro'] and result.get('selected_macro_id') == macro_id
                                    and result.get('level') == args.level and result.get('record_step') == 8
                                    and result.get('recorded_inputs') == expected and saved_events == expected)
            manifest['runs'].append(result)
            save(args.output / 'manifest.json', manifest)
            stop_owned_game(executable)
            launched = False
            result['crash_reports'] = []
            for crash in sorted(set(crash_dir.glob('*.log')) - before_crashes):
                destination = args.output / f'run-{index + 1}-crash-{crash.name}'
                shutil.copy2(crash, destination)
                result['crash_reports'].append(destination.name)
            if result['crash_reports']:
                result['passed'] = False
            save(args.output / 'manifest.json', manifest)
            print(json.dumps(result), flush=True)
            for log in set((args.game / 'Contents/geode/logs').glob('*.log')) - before_logs:
                shutil.copy2(log, args.output / f'run-{index + 1}-{log.name}')
            if not result['passed']:
                break
        return 0 if len(manifest['runs']) == args.runs and all(r['passed'] for r in manifest['runs']) else 1
    finally:
        if launched:
            stop_owned_game(executable)
        for path, data in backups.items():
            if data is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(data)


if __name__ == '__main__':
    sys.exit(main())
