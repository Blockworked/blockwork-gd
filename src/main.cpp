#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>

#include "IpcClient.hpp"

using namespace geode::prelude;

namespace {

// Kept alive for the mod's whole lifetime so the listener never gets torn
// down; ListenerHandle's destructor would otherwise deregister it.
ListenerHandle configPopupCloseListener;

// Tracks the currently-open popup (if any) so pressing the keybind again
// closes it instead of stacking a duplicate on top. Ref keeps it valid for
// as long as we hold it; the close listener below clears it if the popup is
// dismissed some other way (close button, Escape, clicking outside).
Ref<Popup> openConfigPopup;

// Bound to the "open-config" keybind setting (rebindable from this mod's
// settings, like any other keybind), which opens the mod's own settings
// popup - the same one Geode's mod list shows, so there's a single source of
// truth for the options UI instead of a second one to keep in sync.
// listenForKeybindSettingPresses already skips text inputs unless the
// setting opts in, so no manual IME check is needed here.
void setupConfigPopupHotkey() {
    listenForKeybindSettingPresses("open-config", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) {
            return;
        }

        if (openConfigPopup) {
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
            configPopupCloseListener = Popup::CloseEvent(popup).listen([] {
                openConfigPopup = nullptr;
                return ListenerResult::Propagate;
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

// destroyPlayer() (death) and resetLevel() (death's delayed restart, or a
// manual retry) can both want to end the same attempt. This dedupes so
// death only sends the stop command once, right away, instead of again
// when resetLevel() follows it.
bool attemptEndSent = false;

void requestAttemptStart() {
    waitingForAttemptStart = true;
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
        if (waitingForAttemptStart
            && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == static_cast<GJBaseGameLayer*>(this)
            && m_gameState.m_levelTime > 0.0) {
            waitingForAttemptStart = false;
            sendAttemptStart();
        }
    }
};

$on_mod(Loaded) {
    macros::IpcClient::get().start();
    setupConfigPopupHotkey();
}
