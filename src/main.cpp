#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "IpcClient.hpp"

using namespace geode::prelude;

namespace {

// init()/resetLevel() only set this; the actual start command is sent from
// the first GJBaseGameLayer::update() tick afterwards. init() and
// resetLevel() do a lot of synchronous level-parsing/setup work, so sending
// from inside them fires while the level is still loading, not when
// gameplay actually begins simulating. Deferring to the next real tick lines
// the signal up with the moment the level is actually going.
bool pendingAttemptStart = false;

void requestAttemptStart() {
    pendingAttemptStart = true;
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
    if (!Mod::get()->getSettingValue<bool>("stop-on-end")) {
        return;
    }
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

    void resetLevel() {
        PlayLayer::resetLevel();
        // Death or manual retry: end the attempt that just finished now (not
        // time-critical), and request the next attempt's start signal for
        // the next tick.
        sendAttemptEnd();
        requestAttemptStart();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        // Win: no auto-restart, so only end the attempt.
        sendAttemptEnd();
    }
};

class $modify(GJBaseGameLayer) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);
        // Only fire for the currently active PlayLayer (GJBaseGameLayer is
        // also the editor's base layer), and only on the first tick after a
        // start was requested by init()/resetLevel() above.
        if (pendingAttemptStart && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == static_cast<GJBaseGameLayer*>(this)) {
            pendingAttemptStart = false;
            sendAttemptStart();
        }
    }
};

$on_mod(Loaded) {
    macros::IpcClient::get().start();
}
