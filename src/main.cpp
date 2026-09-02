#include "Bot.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

class $modify(GJBaseGameLayer) {
    struct Fields {};

    void update(float dt) override {
        GJBaseGameLayer::update(dt);
        if (auto pl = typeinfo_cast<PlayLayer*>(this)) {
            bot::BotController::get().onUpdate(pl, dt);
        }
    }
};

class $modify(PlayLayer) {
    struct Fields {};

    void resetLevel() override {
        PlayLayer::resetLevel();
        bot::BotController::get().onResetLevel(this);
    }

    void levelComplete() {
        bot::BotController::get().onCompleteLevel();
        PlayLayer::levelComplete();
    }

    void onQuit() {
        bot::BotController::get().onLeaveLevel();
        PlayLayer::onQuit();
    }
};