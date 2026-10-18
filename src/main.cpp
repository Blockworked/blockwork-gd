#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/AppDelegate.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>

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
    int result = (currentAttemptAction == "start_recording")
        ? blockwork_start_recording()
        : blockwork_run_macro(nullptr, elapsedOvershootMs);
    if (result != 0) {
        log::warn("blockwork: {} failed (code {})", currentAttemptAction, result);
    } else {
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

class $modify(MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }
        gameFinishedLoading = true;
        return true;
    }
};

class $modify(PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }
        // First entry into the level. Retries reuse this same PlayLayer and
        // go through resetLevel() instead, never back through init().
        requestAttemptStart();
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        // Death: end the attempt right here, rather than waiting for the
        // resetLevel() that follows once the death animation finishes.
        sendAttemptEnd();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        // Manual retry ends up here with no prior destroyPlayer() call, so
        // this is what ends that attempt. After a death, sendAttemptEnd()
        // was already sent above and is a no-op here. Either way, arm the
        // next attempt's start signal.
        sendAttemptEnd();
        requestAttemptStart();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        // Win: no auto-restart, so only end the attempt.
        sendAttemptEnd();
    }

    void onQuit() {
        PlayLayer::onQuit();
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
    }
};

class $modify(GJBaseGameLayer) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);
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
