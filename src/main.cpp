#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/AppDelegate.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#include <fstream>
#include <Geode/binding/LocalLevelManager.hpp>
#include <Geode/binding/LevelDownloadDelegate.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <cstdio>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>

#include "LevelMacros.hpp"
#include "MacrosPopup.hpp"
#include "blockwork_ffi.h"

using namespace geode::prelude;

namespace {

// Routes every `tracing::info!`/`warn!`/etc. call from the Rust side (both
// blockwork-ffi's own and blockwork-core's, when linked in) into Geode's logger.
// Without this registered, those calls have no subscriber and are silently
// dropped — there is no other way to see Rust-side diagnostics (e.g. Wine
// detection, bridge setup) in Geode's log file.
void onMacrosLog(const char* msg) {
    log::info("[blockwork] {}", msg);
}

// The game's initial boot splash (LoadingLayer) doesn't have all of the UI
// sprite sheets in CCSpriteFrameCache yet, so building any UI before it hands
// off to the main menu gets the pink/black missing-texture placeholder
// instead of the real art. MenuLayer::init is the first reliable, hookable
// signal that boot has actually finished (LoadingLayer::loadingFinished()
// itself is inlined in the game binary, so Geode can't hook it directly);
// it also re-fires every time the player returns to the main menu later,
// which is harmless since this only ever needs to flip on once.
bool gameFinishedLoading = false;

// Tracks OS-level window focus, kept up to date by the AppDelegate hooks
// further down (applicationDidEnterBackground/applicationWillEnterForeground).
// Starts true since the game obviously has focus at launch and neither hook
// fires just from the process starting up. Checked at attempt start so a
// level that begins while the player is tabbed out into another window
// doesn't start recording/running into it; the focus-loss hook itself is
// what handles focus being lost mid-attempt instead.
bool windowFocused = true;

// Kept alive for the mod's whole lifetime so the listener never gets torn
// down; ListenerHandle's destructor would otherwise deregister it.
ListenerHandle configPopupCloseListener;
ListenerHandle macrosPopupCloseListener;

// Tracks the currently-open popup (if any) so pressing the keybind again
// closes it instead of stacking a duplicate on top. Ref keeps it valid for
// as long as we hold it; the close listener below clears it once the popup
// is actually gone (close button, Escape, clicking outside, or our own
// keyBackClicked() call below).
Ref<Popup> openConfigPopup;

// Same idea as openConfigPopup, but for MacrosPopup - a plain custom Popup
// rather than a settings popup, so it has no "unsaved changes" state to
// worry about; keyBackClicked() closes it outright, no closingX guard needed.
Ref<Popup> openMacrosPopup;

// keyBackClicked() doesn't close the settings popup immediately if it has
// uncommitted changes - it puts up an "Unsaved Changes" confirmation first,
// and only sends Popup::CloseEvent (which clears openConfigPopup above) once
// that's resolved. Spamming the hotkey while that confirmation is pending
// must not call keyBackClicked() again on the same popup: that would stack a
// second confirmation on top, and two independent Discard clicks each
// closing the same popup is a use-after-free on the second one. This flag
// makes every press after the first one a no-op until the popup is
// confirmed gone.
bool closingConfigPopup = false;

// Bound to the "open-config" keybind setting (rebindable from this mod's
// settings, like any other keybind), which opens the mod's own settings
// popup - the same one Geode's mod list shows, so there's a single source of
// truth for the options UI instead of a second one to keep in sync.
// listenForKeybindSettingPresses already skips text inputs unless the
// setting opts in, so no manual IME check is needed here.
void setupConfigPopupHotkey() {
    listenForKeybindSettingPresses("open-config", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat || !gameFinishedLoading) {
            return;
        }

        // If the scene hosting the popup got torn down without going through
        // our close path (e.g. the loading screen finishing and handing off
        // to the menu while the popup was open, opened too early during
        // boot), the popup is orphaned but not destroyed - our Ref is still
        // keeping it alive. Trying to close a scene-less zombie popup is
        // what led to the earlier crash, so just forget about it and treat
        // this press as opening a fresh one instead.
        if (openConfigPopup && !openConfigPopup->getParent()) {
            openConfigPopup = nullptr;
            closingConfigPopup = false;
        }

        if (openConfigPopup) {
            if (closingConfigPopup) {
                return;
            }
            closingConfigPopup = true;
            // keyBackClicked() is protected on Popup itself, but public on
            // FLAlertLayer; this runs the same close path as pressing
            // Escape (including the unsaved-changes prompt), not a raw removal.
            static_cast<FLAlertLayer*>(openConfigPopup.data())->keyBackClicked();
            return;
        }

        // Two-arg overload (matching the single-arg wrapper's own default of
        // `true`) is the one that returns the Popup* we need to track.
        if (auto popup = openSettingsPopup(Mod::get(), true)) {
            openConfigPopup = popup;
            closingConfigPopup = false;
            configPopupCloseListener = Popup::CloseEvent(popup).listen([] {
                openConfigPopup = nullptr;
                closingConfigPopup = false;
            });
        }
    });
}

// Bound to the "open-macros" keybind setting, opening the macro
// creation/selection/clear panel (MacrosPopup). Simpler than
// setupConfigPopupHotkey above since MacrosPopup is a plain Popup with no
// unsaved-changes prompt on close - keyBackClicked() always closes it
// immediately, so there's no closing-in-progress state to guard against.
void setupMacrosPopupHotkey() {
    listenForKeybindSettingPresses("open-macros", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat || !gameFinishedLoading) {
            return;
        }

        // Same orphaned-popup defense as setupConfigPopupHotkey: if the
        // scene got torn down without going through our close path, forget
        // the zombie Ref instead of trying to close it.
        if (openMacrosPopup && !openMacrosPopup->getParent()) {
            openMacrosPopup = nullptr;
        }

        if (openMacrosPopup) {
            static_cast<FLAlertLayer*>(openMacrosPopup.data())->keyBackClicked();
            return;
        }

        if (auto popup = MacrosPopup::create()) {
            popup->show();
            openMacrosPopup = popup;
            macrosPopupCloseListener = Popup::CloseEvent(popup).listen([] {
                openMacrosPopup = nullptr;
            });
        }
    });
}

// init()/resetLevel() only arm this; the actual start command is sent once
// GJBaseGameLayer::update() observes the level's own clock (m_gameState.
// m_levelTime) actually ticking. A retry has no loading to do, so that
// happens on the very next tick. A fresh level entry sits behind a scene
// fade-in and, for a not-yet-cached song, an audio-load delay of variable
// length - m_levelTime stays at 0 the whole time, so waiting for it to move
// lines the signal up with when gameplay is actually simulating in both
// cases, instead of guessing at a fixed number of ticks or lifecycle hooks.
bool waitingForAttemptStart = false;

// Real time (accumulated dt, not tick count) since m_levelTime first ticked
// positive. A fresh entry can still be finishing texture decompression and
// sprite-cache population for a moment right as gameplay starts, which
// briefly slows real-world tick time even though the level clock itself is
// already moving - a retry has no such contention. Blockwork's recording clock
// is wall-clock, not level-time, so sending the start trigger mid-hitch
// bakes that attempt-1-only stutter straight into the first recorded Wait.
//
// This used to require a streak of consecutive un-hitched ticks instead of
// a flat time budget, but that made the trigger's own timing noisy: any
// single stray slow tick - for reasons that have nothing to do with loading
// (OS scheduling, monitor refresh jitter, background disk I/O) - reset the
// streak and pushed the trigger out further, so the delay before it fired
// varied attempt to attempt instead of staying constant. Unconditionally
// waiting out a fixed grace period is deterministic in *intent*: every
// attempt, fresh or retry, is meant to get exactly the same delay after
// gameplay visually starts.
//
// In practice this check only runs once per frame, so the trigger still
// fires on whichever frame's update() first pushes the running total past
// kStartGracePeriod - overshooting the exact mark by that frame's own dt,
// which is normally sub-frame noise but grows in lockstep with frame-time
// variance (a Proton/translation-layer stutter landing on this exact frame
// bakes its whole dt into the overshoot). sendAttemptStart() is handed that
// overshoot and forwards it to blockwork_run_macro so the Rust side can
// backdate the run's own timeline to the *intended* instant instead of
// baking this frame-quantization error into it on top of its own dispatch
// latency.
double elapsedSinceLevelStart = 0.0;
constexpr double kStartGracePeriod = 0.2; // seconds

// destroyPlayer() (death) and resetLevel() (death's delayed restart, or a
// manual retry) can both want to end the same attempt. This dedupes so
// death only sends the stop command once, right away, instead of again
// when resetLevel() follows it.
bool attemptEndSent = false;

// Captured once in sendAttemptStart() and reused (not re-read) in
// sendAttemptEnd() — the "action" setting is a live dropdown the user can
// change at any time, including mid-attempt while a recording is still
// active. Re-reading it fresh at end-time meant switching the setting
// before backing out of the level would call stop_loop instead of
// stop_recording, silently orphaning the in-progress recording (it just
// stayed active forever, capturing into a queue nothing ever drained).
// Pairing start/end to the same value fixes that regardless of what the
// user does to the setting mid-attempt.
std::string currentAttemptAction;

// Same capture-once-reuse-at-end reasoning as currentAttemptAction above,
// applied to whether this attempt actually started anything - true only if
// both the "enabled" setting and windowFocused held at attempt-start time.
// If the user flips "enabled" off, or the window loses focus, mid-attempt
// after a recording/run already started while both were fine,
// sendAttemptEnd() still has to send the matching stop - otherwise that
// recording/run is orphaned exactly like a mid-attempt "action" switch
// would orphan it. Conversely, if either was false at attempt start
// (nothing was ever sent), the end must stay a no-op even if both go back
// to true before the attempt finishes.
bool currentAttemptMacrosEnabled = false;


// Frame-locked playback: instead of the wall-clock runner firing OS input,
// the macro's clock is the game's own physics step. Each processCommands tick
// advances the macro to that step's time and the inputs it emits are applied
// right there, so every click lands on the exact step the macro says.
constexpr double kStepsPerSecond = 240.0;
bool frameLockedActive = false;
// True while an imported click's extra update runs; that update must not kill
// the player since it leaves the position untouched.
bool inExtraUpdate = false;
long long frameLockedStep = 0;
GJBaseGameLayer* frameLockedLayer = nullptr;

bool frameLockedEnabled() {
    return Mod::get()->getSettingValue<bool>("frame-locked");
}

// Set while our own playback drives handleButton, so it isn't recorded back.
bool inPlaybackInput = false;
// Held state per player as last seen by the recorder: the game also releases
// buttons that were never held (level start), which isn't an input.
bool recordHeld[2] = {false, false};

// Left mouse button / Space / Up are player 1's jump, Right button is player 2's.
void onSyncInput(const char* name, bool isButton, bool down) {
    if (!frameLockedLayer) {
        return;
    }
    struct Guard { Guard() { inPlaybackInput = true; } ~Guard() { inPlaybackInput = false; } } guard;
    std::string_view n = name;
    if (Mod::get()->getSettingValue<bool>("trace-steps")) {
        log::info("[trace-input] {} {} {} at step {}", name, isButton, down, frameLockedStep);
    }
    // Middle/Back are clicks that also carry the recorder's extra update: one
    // more physics tick for the player's velocity and rotation, with no
    // movement.
    bool extraUpdate = isButton && (n == "Middle" || n == "Back");
    if (extraUpdate) {
        bool p2 = n == "Back";
        auto* player = p2 ? frameLockedLayer->m_player2 : frameLockedLayer->m_player1;
        // The extra tick happens before the click, with the pre-click state.
        auto pos = player->getPosition();
        inExtraUpdate = true;
        // PlayerObject::update takes its delta in 60 Hz units, so one 240 Hz step is 0.25.
        float stepSeconds = 1.f / static_cast<float>(kStepsPerSecond);
        player->preCollision();
        player->update(60.f * stepSeconds);
        frameLockedLayer->checkCollisions(player, stepSeconds, true);
        player->postCollision(stepSeconds, true);
        inExtraUpdate = false;
        player->setPosition(pos);
        frameLockedLayer->handleButton(down, 1, !p2);
        return;
    }
    bool queued = Mod::get()->getSettingValue<bool>("queue-inputs");
    if (isButton ? n == "Left" : (n == "Space" || n == "UpArrow")) {
        if (queued) frameLockedLayer->queueButton(1, down, false, frameLockedLayer->m_timestamp);
        else frameLockedLayer->handleButton(down, 1, true);
    } else if (isButton && n == "Right") {
        if (queued) frameLockedLayer->queueButton(1, down, true, frameLockedLayer->m_timestamp);
        else frameLockedLayer->handleButton(down, 1, false);
    }
}

// Step-stamped recording: real input is recorded against the game's own physics
// step (as the game applies it), not the wall clock, so playback through the
// frame-locked path lands every input on exactly the step it was recorded on.
bool stepRecordingActive = false;
long long recordStep = 0;
FILE* stepFile = nullptr;

// Inputs are collected here and only handed to blockwork at the end, so a
// practice-mode respawn can cut the list back to what it was at a checkpoint:
// the saved macro is then the stitched-together winning run, not every attempt.
struct RecordedInput {
    long long step;
    bool secondary;
    bool down;
};
std::vector<RecordedInput> recordedInputs;

// What the recording looked like when a checkpoint was placed.
struct CheckpointSnapshot {
    size_t inputCount;
    long long step;
    bool held[2];
};
std::map<CheckpointObject*, CheckpointSnapshot> checkpointSnapshots;

// Dead in practice mode: the recording is kept for the respawn to cut back,
// but nothing more may be added (or saved) until then.
bool recordDead = false;
// Set when resetLevel() respawned at a checkpoint rather than restarting.
bool respawnedAtCheckpoint = false;

bool stepRecordingWanted() {
    return frameLockedEnabled() && Mod::get()->getSettingValue<bool>("enabled")
        && Mod::get()->getSettingValue<std::string>("action") == "start_recording";
}

double stepTimeMs(long long step) {
    return static_cast<double>(step - Mod::get()->getSettingValue<int64_t>("frame-offset")) * 1000.0 / kStepsPerSecond;
}

void startStepRecording(GJBaseGameLayer* layer) {
    stepRecordingActive = true;
    recordStep = 0;
    recordHeld[0] = recordHeld[1] = false;
    recordedInputs.clear();
    checkpointSnapshots.clear();
    recordDead = false;
    blockwork_step_record_begin();
    if (Mod::get()->getSettingValue<bool>("substep-clicks")) {
        layer->m_clickBetweenSteps = true;
    }
    levelmacros::linkSelectedTo(layer->m_level);
    log::info("blockwork: step recording started");
}

// Saves (`keep`) or throws away what was recorded.
void endStepRecording(bool keep) {
    if (!stepRecordingActive) {
        return;
    }
    stepRecordingActive = false;
    blockwork_step_record_begin();
    if (!keep || recordDead) {
        recordedInputs.clear();
        checkpointSnapshots.clear();
        recordDead = false;
        return;
    }
    for (auto const& in : recordedInputs) {
        blockwork_step_record_input(stepTimeMs(in.step), in.secondary, in.down);
    }
    recordedInputs.clear();
    checkpointSnapshots.clear();
    int result = blockwork_step_record_end();
    if (result < 0) {
        log::warn("blockwork: step recording save failed (code {})", result);
    } else {
        log::info("blockwork: step recording saved, {} instructions, last step {}", result, recordStep);
    }
}

void stopFrameLocked() {
    if (frameLockedActive) {
        frameLockedActive = false;
        frameLockedLayer = nullptr;
        blockwork_stop_loop();
    }
}

// Arms a frame-locked run from step 0. Returns false if frame-locked
// playback isn't applicable (disabled, or not a macro run).
bool startFrameLocked(GJBaseGameLayer* layer) {
    if (!frameLockedEnabled() || !Mod::get()->getSettingValue<bool>("enabled")
        || Mod::get()->getSettingValue<std::string>("action") != "run_macro") {
        return false;
    }
    stopFrameLocked();
    // Run this level's own macro if one is linked, else whatever is selected.
    levelmacros::selectLinkedFor(layer->m_level);
    int result = blockwork_sync_begin(nullptr);
    if (result != 0) {
        log::warn("blockwork: sync_begin failed (code {})", result);
        return false;
    }
    frameLockedActive = true;
    frameLockedStep = 0;
    frameLockedLayer = layer;
    if (Mod::get()->getSettingValue<bool>("substep-clicks")) {
        layer->m_clickBetweenSteps = true;
    }
    log::info("blockwork: frame-locked run started (offset {})", Mod::get()->getSettingValue<int64_t>("frame-offset"));
    return true;
}


// Test harness: if autotest.json ({"level_id": N}) sits in this mod's save
// dir when the main menu first loads, download that level and start it, so a
// script can drive a full macro run with no clicking. Consumed on use.
// Progress is reported to the Geode log under "[autotest]".
bool autotestLevelOpen = false;

class AutoTestOpener : public LevelDownloadDelegate {
public:
    static AutoTestOpener& get() {
        static AutoTestOpener instance;
        return instance;
    }

    void open(GJGameLevel* level) {
        log::info("[autotest] opening '{}' (level string {} bytes)", std::string(level->m_levelName), level->m_levelString.size());
        GameLevelManager::get()->m_levelDownloadDelegate = nullptr;
        autotestLevelOpen = true;
        // Entered through the level page and its Play button, like a real
        // run, so the game's anti-cheat doesn't see an out-of-flow start.
        CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, LevelInfoLayer::scene(level, false)));
    }

    void levelDownloadFinished(GJGameLevel* level) override {
        open(level);
    }

    void levelDownloadFailed(int response) override {
        log::error("[autotest] level download failed ({})", response);
        GameLevelManager::get()->m_levelDownloadDelegate = nullptr;
    }
};

bool autotestStarted = false;

void maybeStartAutotest() {
    if (autotestStarted) {
        return;
    }
    autotestStarted = true;
    auto path = Mod::get()->getSaveDir() / "autotest.json";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;
    }
    auto parsed = file::readJson(path);
    std::filesystem::remove(path, ec);
    if (!parsed) {
        log::error("[autotest] unreadable autotest.json");
        return;
    }
    auto json = parsed.unwrap();
    if (auto ldm = json["ldm"].asBool()) {
        // Low detail mode is game variable 0042.
        GameManager::get()->setGameVariable("0042", ldm.unwrap());
        log::info("[autotest] low detail mode -> {}", ldm.unwrap());
    }
    if (auto fps = json["fps"].asDouble()) {
        // Frames shorter than a physics step let a real input land on any step.
        CCApplication::sharedApplication()->toggleVerticalSync(false);
        CCApplication::sharedApplication()->setAnimationInterval(1.0 / fps.unwrap());
        log::info("[autotest] fps -> {}", fps.unwrap());
    }
    if (auto newMacro = json["new_macro"].asString()) {
        log::info("[autotest] new_macro('{}') -> {}", newMacro.unwrap(), blockwork_create_macro(newMacro.unwrap().c_str()));
    }
    if (auto importPath = json["import_gdr"].asString()) {
        log::info("[autotest] import_gdr('{}') -> {}", importPath.unwrap(), blockwork_import_gdr(importPath.unwrap().c_str()));
    }
    if (json["import_pending"].asBool().unwrapOr(false)) {
        log::info("[autotest] import_pending -> {}", blockwork_import_pending_gdrs());
    }
    // A local (My Levels) copy by name wins over downloading.
    if (auto localName = json["local_name"].asString()) {
        auto* locals = LocalLevelManager::get()->m_localLevels;
        for (auto* level : CCArrayExt<GJGameLevel*>(locals)) {
            if (std::string(level->m_levelName) == localName.unwrap()) {
                log::info("[autotest] using local level '{}'", localName.unwrap());
                AutoTestOpener::get().open(level);
                return;
            }
        }
        log::error("[autotest] no local level named '{}'", localName.unwrap());
        return;
    }
    int levelId = json["level_id"].asInt().unwrapOr(0);
    log::info("[autotest] starting level {}", levelId);
    auto* glm = GameLevelManager::get();
    auto* level = glm->getSavedLevel(levelId);
    if (level && !level->m_levelString.empty()) {
        AutoTestOpener::get().open(level);
        return;
    }
    glm->m_levelDownloadDelegate = &AutoTestOpener::get();
    glm->downloadLevel(levelId, false, 0);
}

void requestAttemptStart() {
    waitingForAttemptStart = true;
    elapsedSinceLevelStart = 0.0;
    attemptEndSent = false;
}

// `elapsedOvershootMs`: how far past the exact kStartGracePeriod mark
// GJBaseGameLayer::update() overshot before this got called - see its
// comment on the elapsedSinceLevelStart check below. Only meaningful for
// blockwork_run_macro (it backdates that run's first Wait deadline to the
// *intended* start instant); blockwork_start_recording has no timeline of its
// own to backdate, so this is simply ignored on that path.
void sendAttemptStart(double elapsedOvershootMs) {
    // requestAttemptStart() already reset this when the attempt was armed,
    // but anything that fires sendAttemptEnd() during the grace-period
    // window between arming and actually starting (observed in practice:
    // pauseGame, or a rapid death/retry storm right as the level loads)
    // would flip it back to true before the real attempt ever begins -
    // and nothing else would reset it again until the *next* attempt,
    // silently no-op'ing every real stop for this entire attempt. This is
    // the actual start, so this is the point that must be true regardless
    // of anything that happened while waiting for it.
    attemptEndSent = false;
    bool enabledSetting = Mod::get()->getSettingValue<bool>("enabled");
    // A level starting while the player is tabbed out shouldn't start
    // recording/running into whatever window does have focus - the
    // AppDelegate focus-loss hook only covers focus lost *during* an
    // attempt, not an attempt beginning while focus is already elsewhere
    // (e.g. a fast retry landing before the player has tabbed back in).
    currentAttemptMacrosEnabled = enabledSetting && windowFocused;
    if (!currentAttemptMacrosEnabled) {
        log::debug("blockwork: skipping attempt start (enabled={}, windowFocused={})", enabledSetting, windowFocused);
        return;
    }
    currentAttemptAction = Mod::get()->getSettingValue<std::string>("action");
    // Pushed fresh into blockwork-core's own settings file every attempt, same
    // as "action" above - blockwork_run_macro() already re-reads it from there
    // on every call, so this is the one point that needs to keep the two in
    // sync. Harmless (and ignored) when this attempt is recording instead.
    blockwork_set_speed_multiplier(Mod::get()->getSettingValue<double>("speed-multiplier"));
    auto* level = PlayLayer::get() ? PlayLayer::get()->m_level : nullptr;
    bool recording = currentAttemptAction == "start_recording";
    if (!recording) {
        levelmacros::selectLinkedFor(level);
    }
    int result = recording
        ? blockwork_start_recording()
        : blockwork_run_macro(nullptr, elapsedOvershootMs);
    if (result != 0) {
        log::warn("blockwork: {} failed (code {})", currentAttemptAction, result);
    } else {
        if (recording) {
            // The macro being recorded becomes this level's macro.
            levelmacros::linkSelectedTo(level);
        }
        log::debug("blockwork: {} ok (elapsed_overshoot_ms={})", currentAttemptAction, elapsedOvershootMs);
    }
}

void sendAttemptEnd() {
    // TEMPORARY: see exactly why this might bail early.
    log::info(
        "blockwork: sendAttemptEnd called (attemptEndSent={}, stop-on-end={}, currentAttemptAction='{}')",
        attemptEndSent, Mod::get()->getSettingValue<bool>("stop-on-end"), currentAttemptAction
    );
    if (attemptEndSent || !Mod::get()->getSettingValue<bool>("stop-on-end") || !currentAttemptMacrosEnabled) {
        return;
    }
    attemptEndSent = true;
    if (currentAttemptAction == "start_recording") {
        // Positive = instructions captured and saved, 0 = capture hook
        // produced nothing at all, negative = couldn't resolve/save.
        int result = blockwork_stop_recording();
        if (result < 0) {
            log::warn("blockwork: stop_recording failed (code {})", result);
        } else if (result == 0) {
            log::warn("blockwork: stop_recording ok, but captured 0 instructions - input capture isn't picking anything up");
        } else {
            log::info("blockwork: stop_recording ok, captured {} instructions", result);
        }
    } else {
        int result = blockwork_stop_loop();
        if (result != 0) {
            log::warn("blockwork: stop_loop failed (code {})", result);
        } else {
            log::debug("blockwork: stop_loop ok");
        }
    }
}

}

class $modify(BlockworkMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }
        gameFinishedLoading = true;
        // Opening a level straight from init gets overwritten by the menu's
        // own scene swap, so give it a moment to settle first.
        runAction(CCSequence::create(
            CCDelayTime::create(1.5f),
            CCCallFunc::create(this, callfunc_selector(BlockworkMenuLayer::startAutotest)),
            nullptr
        ));
        return true;
    }

    void startAutotest() {
        maybeStartAutotest();
    }
};

class $modify(BlockworkLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) {
            return false;
        }
        if (autotestLevelOpen) {
            runAction(CCSequence::create(
                CCDelayTime::create(1.0f),
                CCCallFunc::create(this, callfunc_selector(BlockworkLevelInfoLayer::autotestPlay)),
                nullptr
            ));
        }
        return true;
    }

    void autotestPlay() {
        log::info("[autotest] pressing Play");
        onPlay(nullptr);
    }
};

class $modify(PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }
        // First entry into the level. Retries reuse this same PlayLayer and
        // go through resetLevel() instead, never back through init().
        if (stepRecordingWanted()) {
            startStepRecording(this);
        } else if (!startFrameLocked(this)) {
            requestAttemptStart();
        }
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (inExtraUpdate) {
            return;
        }
        PlayLayer::destroyPlayer(player, object);
        // The game also calls this for hits it then ignores (the player
        // survives), which must not end the attempt.
        if (!player->m_isDead) {
            return;
        }
        if (autotestLevelOpen) {
            log::info("[autotest] died at {}% on step {} (object id {} anticheat={} at {},{}; player at {},{})", getCurrentPercent(), stepRecordingActive ? recordStep : frameLockedStep, object ? object->m_objectID : -1, object == m_anticheatSpike, object ? object->getPositionX() : 0.f, object ? object->getPositionY() : 0.f, player->getPositionX(), player->getPositionY());
        }
        stopFrameLocked();
        if (stepRecordingActive && m_isPracticeMode && !checkpointSnapshots.empty()) {
            // The respawn will cut the recording back to its checkpoint.
            recordDead = true;
        } else {
            endStepRecording(false);
        }
        // Death: end the attempt right here, rather than waiting for the
        // resetLevel() that follows once the death animation finishes.
        sendAttemptEnd();
    }

    void storeCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::storeCheckpoint(checkpoint);
        if (stepRecordingActive && !recordDead && checkpoint) {
            checkpointSnapshots[checkpoint] = {recordedInputs.size(), recordStep, {recordHeld[0], recordHeld[1]}};
        }
    }

    void removeCheckpoint(bool first) {
        PlayLayer::removeCheckpoint(first);
        std::erase_if(checkpointSnapshots, [this](auto const& kv) {
            return !m_checkpointArray || !m_checkpointArray->containsObject(kv.first);
        });
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::loadFromCheckpoint(checkpoint);
        if (!stepRecordingActive) {
            return;
        }
        auto it = checkpointSnapshots.find(checkpoint);
        if (it == checkpointSnapshots.end()) {
            return;
        }
        // Stitch: drop everything recorded after the checkpoint and carry on
        // from its step, so only the attempt that gets through is kept.
        auto const& snap = it->second;
        recordedInputs.resize(std::min(recordedInputs.size(), snap.inputCount));
        recordStep = snap.step;
        recordHeld[0] = snap.held[0];
        recordHeld[1] = snap.held[1];
        recordDead = false;
        respawnedAtCheckpoint = true;
        log::info("blockwork: practice respawn, recording cut back to step {} ({} inputs)", snap.step, recordedInputs.size());
    }

    void resetLevel() {
        respawnedAtCheckpoint = false;
        PlayLayer::resetLevel();
        if (respawnedAtCheckpoint && stepRecordingActive) {
            // Practice respawn: the recording continues from the checkpoint.
            respawnedAtCheckpoint = false;
            return;
        }
        // Manual retry ends up here with no prior destroyPlayer() call, so
        // this is what ends that attempt. After a death, sendAttemptEnd()
        // was already sent above and is a no-op here. Either way, arm the
        // next attempt's start signal.
        sendAttemptEnd();
        stopFrameLocked();
        endStepRecording(false);
        if (stepRecordingWanted()) {
            startStepRecording(this);
        } else if (!startFrameLocked(this)) {
            requestAttemptStart();
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (frameLockedActive || stepRecordingActive) {
            log::info("[autotest] LEVEL COMPLETE on step {}", stepRecordingActive ? recordStep : frameLockedStep);
        }
        stopFrameLocked();
        endStepRecording(true);
        // Win: no auto-restart, so only end the attempt.
        sendAttemptEnd();
    }

    void onQuit() {
        PlayLayer::onQuit();
        stopFrameLocked();
        endStepRecording(true);
        // Leaving the level back to the menu: end the attempt in progress.
        sendAttemptEnd();
    }

    void pauseGame(bool unfocused) {
        PlayLayer::pauseGame(unfocused);
        // TEMPORARY: confirm this hook actually fires on Escape at all
        // before trusting anything past this line.
        log::info("blockwork: pauseGame hook fired (unfocused={})", unfocused);
        // Opening the pause menu (Escape, or losing window focus) isn't
        // gameplay - without this, whatever the player does in the pause
        // menu (navigating to this mod's own config popup, in particular)
        // gets captured right along with real gameplay input. Reuses
        // sendAttemptEnd() as-is: same stop-on-end setting, same action
        // pairing, and a no-op if the attempt already ended (e.g. paused
        // after already dying).
        sendAttemptEnd();
        stopFrameLocked();
        // Pausing in practice mode isn't the end of the run - the stitched
        // recording carries on after resuming.
        if (!(stepRecordingActive && m_isPracticeMode)) {
            endStepRecording(true);
        }
    }
};

class $modify(GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (frameLockedActive && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
            double timeMs = static_cast<double>(frameLockedStep - Mod::get()->getSettingValue<int64_t>("frame-offset")) * 1000.0 / kStepsPerSecond;
            if (frameLockedStep < 60000 && Mod::get()->getSettingValue<bool>("trace-steps")) {
                log::info("[trace] {} {} {}", frameLockedStep, m_player1->getPositionX(), m_player1->getPositionY());
            }
            blockwork_sync_step(timeMs, &onSyncInput);
            frameLockedStep++;
        }
        bool isPlay = static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this;
        if (stepRecordingActive && isPlay && recordStep < 9000 && Mod::get()->getSettingValue<bool>("trace-steps")) {
            log::info("[trace] {} {} {}", recordStep, m_player1->getPositionX(), m_player1->getPositionY());
        }
        if (autotestLevelOpen && isPlay && stepRecordingActive) {
            // Lets the test input generator follow the game's own step clock.
            if (!stepFile) {
                stepFile = fopen("Z:\\tmp\\bw_step.txt", "wb");
            }
            if (stepFile) {
                fseek(stepFile, 0, SEEK_SET);
                fprintf(stepFile, "%lld %d              \n", recordStep, 1);
                fflush(stepFile);
            }
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (stepRecordingActive && isPlay) {
            recordStep++;
        }
    }

    void processQueuedButtons(float dt, bool clearInputQueue) {
        if (stepRecordingActive && !inPlaybackInput && !m_queuedButtons.empty()
            && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
            for (auto const& c : m_queuedButtons) {
                log::info("[trace-queue] rec={} cur={} dt={:.9f} btn={} push={} p2={} step={} ts={:.9f} m_ts={:.9f} lvlTime={:.9f}", recordStep, m_currentStep, dt, (int)c.m_button, c.m_isPush, c.m_isPlayer2, c.m_step, c.m_timestamp, m_timestamp, m_gameState.m_levelTime);
            }
        }
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        // While a macro drives the player, real clicks and keys must not reach
        // it: a stray press would shift the run off the recorded inputs.
        if (frameLockedActive && !inPlaybackInput && button == 1
            && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
            return;
        }
        if (stepRecordingActive && !recordDead && !inPlaybackInput && button == 1
            && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
            bool& held = recordHeld[isPlayer1 ? 0 : 1];
            if (held != down) {
                held = down;
                recordedInputs.push_back({recordStep, !isPlayer1, down});
            }
            if (Mod::get()->getSettingValue<bool>("trace-steps")) {
                log::info("[trace-record] step {} p1={} down={}", recordStep, isPlayer1, down);
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void update(float dt) {
        GJBaseGameLayer::update(dt);
        if (autotestLevelOpen && stepRecordingActive && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
            static int frames = 0;
            static double acc = 0;
            frames++;
            acc += dt;
            if (acc >= 5.0) {
                log::info("[autotest] avg fps {:.1f}", frames / acc);
                frames = 0;
                acc = 0;
            }
        }
        // Only fire for the currently active PlayLayer (GJBaseGameLayer is
        // also the editor's base layer), and only once the level clock is
        // actually moving - that's the real start of gameplay, whether this
        // tick comes right after a retry or several ticks into a fresh
        // entry's loading pause.
        if (!waitingForAttemptStart
            || static_cast<GJBaseGameLayer*>(PlayLayer::get()) != static_cast<GJBaseGameLayer*>(this)
            || m_gameState.m_levelTime <= 0.0) {
            return;
        }

        elapsedSinceLevelStart += dt;
        if (elapsedSinceLevelStart >= kStartGracePeriod) {
            waitingForAttemptStart = false;
            // How far this frame's dt pushed us past the exact target -
            // see the comment on kStartGracePeriod above for why this
            // needs to be measured and forwarded rather than just eaten.
            double overshootMs = (elapsedSinceLevelStart - kStartGracePeriod) * 1000.0;
            sendAttemptStart(overshootMs);
        }
    }
};

class $modify(BlockworkKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        if (stepRecordingActive && Mod::get()->getSettingValue<bool>("trace-steps")) {
            log::info("[trace-key] step={} key={} down={} repeat={}", recordStep, (int)key, down, repeat);
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};

class $modify(AppDelegate) {
    void applicationDidEnterBackground() {
        AppDelegate::applicationDidEnterBackground();
        windowFocused = false;
        // The whole game window losing OS focus (alt-tab, clicking another
        // window, the screen locking, ...) - not the same thing as the
        // in-level pause menu opening, and not scoped to PlayLayer at all:
        // this fires no matter what's on screen (menu, editor, a level).
        // Recording and running both drive real OS-level input
        // (SendInput/CGEvent), so leaving either active once some other
        // window has focus means every subsequent keystroke/click either
        // gets recorded into the macro or gets played back into whatever
        // app the user tabbed into, instead of GD. Stopping both
        // unconditionally here - ignoring "enabled" and "stop-on-end",
        // which are attempt-lifecycle policy, not input-safety - and
        // independently of sendAttemptEnd()/attemptEndSent's own
        // bookkeeping is what makes this a hard safety net rather than
        // just another path to the same attempt-end logic. Both calls are
        // no-ops if nothing of that kind was active.
        int recordingResult = blockwork_stop_recording();
        if (recordingResult < 0) {
            log::warn("blockwork: stop_recording on focus loss failed (code {})", recordingResult);
        }
        int loopResult = blockwork_stop_loop();
        if (loopResult != 0) {
            log::warn("blockwork: stop_loop on focus loss failed (code {})", loopResult);
        }
    }

    void applicationWillEnterForeground() {
        AppDelegate::applicationWillEnterForeground();
        // Regaining focus doesn't resume anything on its own - it just
        // clears the guard sendAttemptStart() checks, so the *next* attempt
        // (not the one already in progress, if any) is allowed to
        // record/run again.
        windowFocused = true;
    }
};

$on_mod(Loaded) {
    // Must be registered before blockwork_init (or anything else that might
    // log) so no diagnostics from setup itself get silently dropped.
    blockwork_set_log_callback(&onMacrosLog);

    // Must run on the main thread — macOS's Accessibility prompt
    // (behind blockwork_init) requires it, and $on_mod(Loaded) already runs
    // there.
    auto dir = Mod::get()->getSettingValue<std::string>("blockwork-dir");
    // Resolved unconditionally (cheap, harmless if unused) — blockwork_init
    // only actually uses this if it detects it's running under Wine on a
    // Linux host (Proton), where WH_KEYBOARD_LL/WH_MOUSE_LL and SendInput
    // don't work, so it launches this as a native Linux helper process
    // instead. Native Windows/macOS ignore it entirely.
    auto bridgePath = CCFileUtils::get()->fullPathForFilename("linux-input.so"_spr, true);
    int result = blockwork_init(dir.empty() ? nullptr : dir.c_str(), bridgePath.empty() ? nullptr : bridgePath.c_str());
    if (result != 0) {
        log::warn("blockwork: blockwork_init failed (code {}) - no input backend for this platform", result);
    } else if (blockwork_grab_failed()) {
        log::warn("blockwork: input capture/emission unavailable (grab failed) - check permissions");
    } else {
        log::info("blockwork: initialized, config dir override = '{}'", dir.empty() ? "<none, using OS default>" : dir);
    }
    setupConfigPopupHotkey();
    setupMacrosPopupHotkey();
}
