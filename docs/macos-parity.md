# macOS integration verification

The live acceptance fixture is the local **Bloodbath Infinity 2** level and fresh imports of `~/Downloads/ChesszDC-Bloodbath-Infinity-zBot.gdr`. The existing user replay is not used as the acceptance macro.

Current status: local-level automation now uses the actual My Levels `EditLevelLayer` Play path. A fresh normal-speed completion and 3/3 queued+CBS+foreign-input 10x smoke runs passed through that path. Earlier frame-clock acceptance results below used the local level data through the online-info page and remain historical evidence, not proof of the corrected entry flow. The earlier dictionary SIGSEGV is still unexplained. A separate allocator/overlay diagnostic later caught a timer SIGBUS; its relationship to the earlier failure is unproven.

## Platform fix

The macOS game inlines `GJBaseGameLayer::processCommands` into `update`. Its standalone function is hookable but is not called by normal play. Before the fix, a fresh import succeeded, playback remained at step zero, and the player died at 1.36%.

macOS now advances host-clock playback and recording at the called `processQueuedButtons` boundary, before the game consumes input. Zero-delta cleanup calls do not advance the clock. Windows retains its original `processCommands` boundary. The Mac build links the current sibling Blockwork engine and the AppKit/CoreGraphics frameworks required by its native backend.

The native test runner launches through Steam, imports the GDR anew for every attempt, links the exact new macro to the requested local level, and records a result carrying a unique run identifier, macro identifier, selected identifier, level, step, completion/death state and elapsed time. The runner rejects results for another run or an existing macro. It keeps each imported macro and the logs in the evidence directory. By default QOL safe mode is on and unrelated mods are disabled. `--keep-profile` preserves the enabled mods and QOL safe-mode/music preferences for acceptance in the actual user environment. Noclip is off in both modes. Configuration and game saves are backed up and restored after the owned game process stops.

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

The initial private CI jobs could not start because of GitHub billing/spending limits. After the user restored runner availability, clean CI exposed and repaired the Codegen mismatch described below. Windows and universal Mac builds then passed: https://github.com/Blockworked/blockwork-gd/actions/runs/40236673481. The Mac binary contains both arm64 and x86_64 with a macOS 11.0 minimum. Intel game runtime is not claimed.

A separate universal smoke batch was interrupted on focus loss during its second attempt. That batch is recorded separately and is not counted among completed batches. The harness now reports paused/unfocused interruptions explicitly. Final evidence is also preserved in `build/macos-verification/summary.json`.

## Recording hook verification

The opt-in `--recording-probe` uses the same local level and a fresh imported macro, then delivers both players' button transitions directly through the game's hooked input entry at steps 1, 2, 3 and 4. Each transition is sent twice. At step 8 it saves the recording and reports `recording-probe-complete`, distinct from full-level completion. The Python runner checks both the observed transitions and the actual persisted macro's reconstructed timestamps, including the configured frame offset. Normal-speed and QOL 10x probes passed with exactly four saved transitions; duplicates were suppressed. An earlier, longer probe collided with an introductory hazard at step 63 and is retained separately as a failed fixture attempt, not a full-level acceptance result.

The rebuilt universal package then passed 3/3 fresh-import queued+CBS+foreign-input 10x completions, followed by another normal-speed and 10x trace pair. Both completed the level. All 724 emitted input events matched exactly in name, direction and step. Position traces matched through step 26,841, after the final input at 26,741; later differences belong to the finish sequence. Evidence: `build/macos-verification/final-normal-vs-10x-trace.json` and the corresponding final manifests.

```sh
python3 scripts/autotest-macos.py --recording-probe --speed 1 --trace --output /tmp/blockwork-recording-normal-new
python3 scripts/autotest-macos.py --recording-probe --speed 10 --cbs --trace --output /tmp/blockwork-recording-10x-new
```

These short recording probes are not counted as level completions. The full replay acceptance remains a separate gate.

Clean CI exposed a separate generator mismatch: Geode's default path downloads a mutable prebuilt Codegen binary, which emitted newer `Geode/mapping/*` includes for the older pinned bindings. Both platforms failed before compiling the mod. CI now builds Codegen from the same pinned bindings source (`SKIP_BUILDING_CODEGEN=OFF`), matching the working local preset; no SDK sources are modified.

## Initial CI artifact acceptance

The successful Mac CI artifact was downloaded and installed for the final checks (SHA-256 `3b240ef6c01ccd1735a1b5b2890a37b6655919f66ee1984f9df4e505ca4f8d00`). It completed one fresh normal-speed queued+CBS run and 3/3 fresh queued+CBS+foreign-input QOL 10x runs. All three 10x schedules matched the normal run's 724 emitted input events exactly, including directions and physics steps. Position traces matched through step 26,840; the final input was step 26,741. Differences began in the finish sequence. Separate recording probes passed at both speeds, including persisted timestamps and duplicate suppression. Startup logs reported no input-grab, initialization, recording-save or host-clock timeout errors.

At that checkpoint the CI artifact and its latest successful fresh replay were installed and linked to the local Bloodbath Infinity 2 level; the later profile acceptance below supersedes that installed package. Nine configuration/game-save files were verified byte-identical after testing, before the intentional final selection/link; the two original user macros remain byte-identical. Temporary test macros were archived outside the configuration directory.

Evidence: `build/macos-verification/ci-40236673481.json`, `ci-normal-vs-10x-traces.json`, and the four `blockwork-gd-macos-ci-*.json` manifests. Finite successful batches establish their observed 100% completion rate under the recorded configuration; they do not prove every future hardware or unrelated-mod configuration.

The local development preset now also targets macOS 11.0 and links the isolated universal FFI archive, matching the verified package's deployment floor. The README uses the same FFI build script, preventing the desktop Qt app's macOS 13 cache from entering this build.

## Actual user profile and startup diagnostics

The installed CI artifact was also tested with custom keybinds 2.2.2, devtools 1.14.0, Click Between Frames 1.5.0, QOL Mod 2.8.6 and Geode 5.11.2 enabled, with QOL safe mode off and music enabled. One fresh normal-speed run completed, as did 19 of 20 fresh queued+CBS+foreign-input 10x attempts. All 19 completed runs emitted the normal run's exact 724-input schedule. Both recording probes passed. The twentieth attempt suffered a real SIGSEGV during `PlayLayer::init`, before playback, while Foundation parsed the game's `glitterEffect.plist`; no mod is identified as the cause. This failure remains in the evidence and is not counted as a successful attempt. The same asset parsed successfully 160,000 times in an independent concurrent Foundation stress fixture; that does not establish the cause of the in-game failure.

Investigation also found two runner defects. Geode treats SIGTERM as a crash, so routine forced shutdowns were generating false crash reports and triggering recovery UI on the next launch. The runner now uses SIGINT, which Geode does not classify as a crash, and preserves any new crash reports per attempt. QOL accelerates Cocos scene delays, so the menu and level-page settling intervals now use steady-clock deadlines. These are test lifecycle corrections, not a demonstrated fix for the SIGSEGV. Startup phase receipts persist the fresh-import identity before entering level initialization so failed starts remain attributable without relying on buffered logs.

```sh
python3 scripts/autotest-macos.py --keep-profile --queue --cbs --stray-queue --trace --speed 10 --runs 20 --output /tmp/blockwork-gd-user-profile-new
```

Evidence: `build/macos-verification/blockwork-gd-macos-user-profile-*.json`, `user-profile-normal-vs-10x-traces.json`, `user-profile-level-init-sigsegv.log`, and `particle-plist-standalone-stress.json`.

## Final CI package and user-state verification

[CI run 40262690447](https://github.com/Blockworked/blockwork-gd/actions/runs/40262690447) passed Windows and universal Mac builds at `9727bed`. The downloaded Mac package SHA-256 is `7b59595f2ee65330734b211ba0ddf36bee0c6f0e019693adb5aa2728b5cc1755`; both architecture slices target macOS 11.0. This package remains installed.

With the actual user profile preserved, the final CI package completed 20/20 fresh-import queued+CBS+foreign-input QOL 10x runs, with no new crash reports. Every run retained a valid startup receipt and rejected the injected foreign input. All 724 input events exactly matched the preceding normal-speed run built from the same C++ source; positions matched through step 26,844, beyond the final input at 26,741. That normal-speed run completed at 100% in 114.70 seconds. Separate normal-speed and 10x recording probes both passed, including saved timestamps and duplicate suppression. The preceding SIGINT-only batch also completed 20/20 with no new crash reports. Neither later batch reproduces or proves the cause of the earlier SIGSEGV.

After all owned game processes stopped, all ten configuration/save files matched their backups byte-for-byte. The two original user macros also remain byte-identical. Test imports were archived outside the live configuration, and the latest successful full replay (`f458dad4d3cf463fb0a4f108db4d0588`) was intentionally renamed, selected and linked to local Bloodbath Infinity 2. QOL speed, safe mode, enabled mods and the remaining preferences were restored.

Evidence: `build/macos-verification/ci-40262690447.json`, `wallclock-ci-normal-vs-10x-traces.json`, `final-settings-restoration.json`, the `blockwork-gd-macos-wallclock-*.json` manifests, and `user-profile-runtime-evidence.tar.gz`. The archive preserves raw logs, macro copies and manifests, including the failed attempt. The earlier failed start's likely imported macro is separately identified by timestamp and matching instruction payload in `failed-start-import-candidate.json`; it is not retroactively marked as a verified successful run.

## Native startup diagnostics

Two opt-in diagnostics isolate initialization from full-level playback. `--particle-plist-probe COUNT` calls the game's exact `CCDictionary::createWithContentsOfFileThreadSafe` path for `glitterEffect.plist`, checks all 51 keys, releases each owned dictionary, and records the resolved asset's SHA-256. Two 10-launch batches completed 100,000 parses each: one with normal startup state, one with Geode's previous-crash marker simulated. All 200,000 native parses succeeded without a new crash report. The asset hash remains `8140a4d21dd2be18acd41c4fcdf4e341201b743fcdf373ce464ee0eb66847b83`. This is stronger coverage of the original loading stack than the separate Foundation-only helper, but it does not reproduce the complete PlayLayer initialization or establish the earlier failure's cause.

`--startup-probe` opens the local level using a new import and waits until the next frame after all mod initialization hooks return, then reports `startup-probe-complete`. It verifies the exact level, fresh/selected macro identity and a live player; it does not play the entire level. `--simulate-previous-crash` recreates Geode recovery state for either diagnostic, restores the previous marker afterward, and retains a real crash's recovery flag if a new crash occurs. The startup result also checks whether the actual recovery popup is present in the game's scene. `--legacy-startup-timing`, available only with the startup probe, reproduces the former speed-scaled scene delays for a controlled comparison; regular playback always uses wall-time startup guards.

```sh
python3 scripts/autotest-macos.py --keep-profile --speed 10 --particle-plist-probe 10000 --runs 10 --output /tmp/blockwork-plist-new
python3 scripts/autotest-macos.py --keep-profile --speed 10 --startup-probe --simulate-previous-crash --legacy-startup-timing --runs 30 --output /tmp/blockwork-startup-legacy-new
python3 scripts/autotest-macos.py --keep-profile --speed 10 --startup-probe --simulate-previous-crash --runs 30 --output /tmp/blockwork-startup-walltime-new
```

Every diagnostic launch still imports the GDR freshly. Diagnostic success has a separate manifest gate and outcome and is never counted as a full-level completion. The production CI package is restored after local diagnostic-package tests.


The complete initialization comparison also passed: 30/30 fresh-import launches with the legacy action-clock timing, and 30/30 with wall-time timing. Every launch confirmed the actual Geode recovery popup before opening the local level, returned from all initialization hooks with a live player, and produced no new crash report. Thus neither the original plist parse path nor the timing/recovery combination has reproduced the one earlier SIGSEGV. That crash remains unexplained; these negative experiments are not proof of a causal repair. All eleven backed-up configuration/save/recovery-state files were verified restored, the original user macros remain byte-identical, and all eighty diagnostic imports were archived. The previously selected successful full replay remains selected. Evidence: `build/macos-verification/native-startup-diagnostics-evidence.tar.gz`, the four diagnostic manifests and `startup-diagnostic-settings-restoration.json`.

## Local entry fidelity and allocator diagnostics

The earlier harness opened local editor levels through `LevelInfoLayer`, the online level-info page. The My Levels route instead uses `EditLevelLayer::scene(level)` and its native `onPlay` handler. Automation now selects that route for `GJLevelType::Editor`; downloaded levels retain their online-info route. Results persist `entry_page` and `level_type`, and the runner requires `local-edit` for local full-playback and recording acceptance. Startup receipts also mark page construction before entering game code. A new fresh normal run completed through `local-edit` with level type 2 at 100% in 113.90 seconds; 3/3 fresh queued+CBS+foreign-input 10x smoke runs also completed. This corrects test fidelity without claiming it proves a native-crash cause.

Optional startup controls retain the old page (`--legacy-local-info`) and action-clock delays (`--legacy-startup-timing`). `--malloc-diagnostics` uses Valve's [documented Steam SDK debugging context](https://partner.steamgames.com/doc/api/steam_api) in a separate owned working directory, records allocator variables from the game process, and enables scribbling, large-allocation guard pages and periodic heap checks that abort on corruption. The installed game executable and resources are not replaced. Native image receipts distinguish direct debugging from Steam's actual loader/overlay injection. `--steam-overlay-diagnostics` adds and verifies those libraries for direct debugging; `--allocator-via-steam` instead verifies allocator variables inherited through Steam's normal launcher from a temporary diagnostic client. Normal acceptance always launches through Steam.

Ten direct SDK startups with heap checks passed. Their missing Steam loader/overlay libraries were then identified by comparison with a normal Steam-launched startup. Adding those libraries produced 3 successful legacy-timing startups followed by a real SIGBUS into a non-code address from `CCTimer::update` on launch 4. The batch stopped. That is a distinct stack from the original plist failure and is not asserted to share its cause. The exact diagnostic package, failure manifest, native crash report and launcher log are preserved as `build/macos-verification/malloc-overlay-timer-sigbus-*`.

A pilot through a temporarily instrumented Steam client confirmed the real Steam launch inherited all eight requested allocator settings and both injected libraries; the current local page initialized. An explicit legacy-page control also initialized, but an erroneous duplicate local-page assertion initially marked that control failed. The contradiction was removed before the larger comparison, and the original failed verifier and manifest are retained. The final Steam restoration uses LaunchServices so its client is independent of the test driver's process group. No Rosetta installation is performed; Intel gameplay remains pending at the user's request.
