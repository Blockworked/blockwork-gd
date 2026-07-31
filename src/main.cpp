#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>

#include "IpcClient.hpp"

using namespace geode::prelude;

namespace {

// The game's initial boot splash (LoadingLayer) doesn't have all of the UI
// sprite sheets in CCSpriteFrameCache yet, so building any UI before it hands
// off to the main menu gets the pink/black missing-texture placeholder
// instead of the real art. MenuLayer::init is the first reliable, hookable
// signal that boot has actually finished (LoadingLayer::loadingFinished()
// itself is inlined in the game binary, so Geode can't hook it directly);
// it also re-fires every time the player returns to the main menu later,
// which is harmless since this only ever needs to flip on once.
bool gameFinishedLoading = false;

// Kept alive for the mod's whole lifetime so the listener never gets torn
// down; ListenerHandle's destructor would otherwise deregister it.
ListenerHandle configPopupCloseListener;

// Tracks the currently-open popup (if any) so pressing the keybind again
// closes it instead of stacking a duplicate on top. Ref keeps it valid for
// as long as we hold it; the close listener below clears it once the popup
// is actually gone (close button, Escape, clicking outside, or our own
// keyBackClicked() call below).
Ref<Popup> openConfigPopup;

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
// already moving - a retry has no such contention. Macros' recording clock
// is wall-clock, not level-time, so sending the start trigger mid-hitch
// bakes that attempt-1-only stutter straight into the first recorded Wait.
//
// This used to require a streak of consecutive un-hitched ticks instead of
// a flat time budget, but that made the trigger's own timing noisy: any
// single stray slow tick - for reasons that have nothing to do with loading
// (OS scheduling, monitor refresh jitter, background disk I/O) - reset the
// streak and pushed the trigger out further, so the delay before it fired
// varied attempt to attempt instead of staying constant. Unconditionally
// waiting out a fixed grace period is deterministic: every attempt, fresh or
// retry, gets exactly the same delay after gameplay visually starts,
// regardless of whether any individual tick happened to hitch.
double elapsedSinceLevelStart = 0.0;
constexpr double kStartGracePeriod = 0.2; // seconds

// destroyPlayer() (death) and resetLevel() (death's delayed restart, or a
// manual retry) can both want to end the same attempt. This dedupes so
// death only sends the stop command once, right away, instead of again
// when resetLevel() follows it.
bool attemptEndSent = false;

void requestAttemptStart() {
    waitingForAttemptStart = true;
    elapsedSinceLevelStart = 0.0;
    attemptEndSent = false;
}

void sendAttemptStart() {
    auto action = Mod::get()->getSettingValue<std::string>("action");
    if (action == "start_recording") {
        macros::IpcClient::get().sendCommand(R"({"cmd":"start_recording"})");
    } else {
        macros::IpcClient::get().sendCommand(R"({"cmd":"run_macro"})");
    }
}

void sendAttemptEnd() {
    if (attemptEndSent || !Mod::get()->getSettingValue<bool>("stop-on-end")) {
        return;
    }
    attemptEndSent = true;
    auto action = Mod::get()->getSettingValue<std::string>("action");
    if (action == "start_recording") {
        macros::IpcClient::get().sendCommand(R"({"cmd":"stop_recording"})");
    } else {
        macros::IpcClient::get().sendCommand(R"({"cmd":"stop_loop"})");
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
            sendAttemptStart();
        }
    }
};

$on_mod(Loaded) {
    macros::IpcClient::get().start();
    setupConfigPopupHotkey();
}
