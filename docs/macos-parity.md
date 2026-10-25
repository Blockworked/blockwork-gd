# macOS integration verification

The live acceptance fixture is the local **Bloodbath Infinity 2** level and fresh imports of `~/Downloads/ChesszDC-Bloodbath-Infinity-zBot.gdr`. The existing user replay is not used as the acceptance macro.

## Platform fix

The macOS game inlines `GJBaseGameLayer::processCommands` into `update`. Its standalone function is hookable but is not called by normal play. Before the fix, a fresh import succeeded, playback remained at step zero, and the player died at 1.36%.

macOS now advances host-clock playback and recording at the called `processQueuedButtons` boundary, before the game consumes input. Zero-delta cleanup calls do not advance the clock. Windows retains its original `processCommands` boundary. The Mac build links the current sibling Blockwork engine and the AppKit/CoreGraphics frameworks required by its native backend.

The native test runner launches through Steam, imports the GDR anew for every attempt, links the exact new macro to the requested local level, and records a result carrying a unique run identifier, macro identifier, selected identifier, level, step, completion/death state and elapsed time. The runner rejects results for another run or an existing macro. It keeps each imported macro and the logs in the evidence directory. QOL safe mode is on and noclip is off; unrelated mods are disabled. Configuration and game saves are backed up and restored after the owned game process stops.

Queued playback also needed an ownership fix: the input-blocking hook was rejecting the mod’s own clicks when the game consumed them after the enqueue callback returned. Playback now retains the exact queued-command identities, filters foreign entries, and marks its own consumption scope. A stray queued press injected before the first recorded jump was rejected while the full 10x replay completed. Its 724 input events and all positions through the input phase match direct playback. The user’s queue + click-between-steps options completed 20/20 fresh-import 10x attempts; each injected foreign press was rejected, with no host-clock timeout warnings. The final queued implementation also completed a fresh normal-speed run at 100% (114.22 seconds). Evidence: `/tmp/blockwork-gd-macos-10x-queued-batch-20/manifest.json` and `/tmp/blockwork-gd-macos-normal-queued-final/manifest.json`.

## Observed results

Environment: macOS 27.0.1, Apple Silicon, Geometry Dash 2.2081, Geode 5.11.2, bindings `1605f500`, QOL Mod 2.8.6. GDR SHA-256: `5700a2eb9213f2cc8e4303ec1966c169145804770048f9f12f733fe4d3b94efe`.

| Gate | Evidence |
|---|---|
| Normal-speed completion | Fresh import completed the local level at 100%, step 27,081, 114.18 seconds from PlayLayer initialization. `/tmp/blockwork-gd-macos-physics-hook-1/manifest.json` |
| QOL 10x completion | Another fresh import completed at 100%, approximately 11.68 seconds. `/tmp/blockwork-gd-macos-10x-trace-1/manifest.json` |
| Input accuracy | All 724 emitted input events had identical names, directions and step numbers at normal speed and 10x. Position traces matched through step 26,840; the final input was step 26,741. Differences began during the finish sequence. `/tmp/blockwork-gd-macos-normal-vs-10x-trace.json` |
| Repeated 10x acceptance | 20/20 completed successfully using 20 distinct fresh imports, with no host-clock timeout warnings. Level time ranged from 11.67–11.74 seconds. `/tmp/blockwork-gd-macos-10x-batch-20/manifest.json`. |

`getCurrentPercent()` may still be slightly below 100 at the game's `levelComplete` callback because it derives progress from position. The authoritative success event is the game completing the level with the player alive; the raw percentage is retained in the result, not rounded to manufacture success.

## Reproduce

Follow the macOS build steps in the README, close GD and the Blockwork daemon, then run:

```sh
python3 scripts/autotest-macos.py --runs 1 --speed 1 --trace --output /tmp/blockwork-gd-normal-new
python3 scripts/autotest-macos.py --runs 20 --speed 10 --output /tmp/blockwork-gd-10x-new
```

The output directory must not already exist. `--queue`, `--cbs`, `--offset` and `--trace` explicitly select developer options. The successful reference runs use offset -1, queue off and click-between-steps off. Finite successful runs establish those observations; they do not prove success under every future machine load or unrelated mod configuration.

Computer-use access to Geometry Dash was rejected by that tool’s approval review. No computer-use controls were used for the game; execution and evidence came from the mod’s own test harness and the official Steam launcher. Visual/keybind interaction coverage is therefore not claimed.

The private-repository CI jobs were rejected before startup by GitHub billing/spending limits. This is an external execution block, not a compiler result. The local universal mod compiles and links both architectures with a macOS 11.0 floor and no mixed-target warnings. Its final 3/3 fresh-import 10x acceptance batch passed on Apple Silicon; Windows compilation and Intel game runtime are not claimed.

A separate universal smoke batch was interrupted on focus loss during its second attempt. That batch is recorded separately and is not counted among completed batches. The harness now reports paused/unfocused interruptions explicitly. Final evidence is also preserved in `build/macos-verification/summary.json`.

## Recording hook verification

The opt-in `--recording-probe` uses the same local level and a fresh imported macro, then delivers both players' button transitions directly through the game's hooked input entry at steps 1, 2, 3 and 4. Each transition is sent twice. At step 8 it saves the recording and reports `recording-probe-complete`, distinct from full-level completion. The Python runner checks both the observed transitions and the actual persisted macro's reconstructed timestamps, including the configured frame offset. Normal-speed and QOL 10x probes passed with exactly four saved transitions; duplicates were suppressed. An earlier, longer probe collided with an introductory hazard at step 63 and is retained separately as a failed fixture attempt, not a full-level acceptance result.

The rebuilt universal package then passed 3/3 fresh-import queued+CBS+foreign-input 10x completions, followed by another normal-speed and 10x trace pair. Both completed the level. All 724 emitted input events matched exactly in name, direction and step. Position traces matched through step 26,841, after the final input at 26,741; later differences belong to the finish sequence. Evidence: `build/macos-verification/final-normal-vs-10x-trace.json` and the corresponding final manifests.

```sh
python3 scripts/autotest-macos.py --recording-probe --speed 1 --trace --output /tmp/blockwork-recording-normal-new
python3 scripts/autotest-macos.py --recording-probe --speed 10 --cbs --trace --output /tmp/blockwork-recording-10x-new
```

These short recording probes are not counted as level completions. The full replay acceptance remains a separate gate.
