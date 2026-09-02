#include "Bot.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>

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
        if (bot::BotController::get().onCompleteLevel()) {
            // Safe mode: do not save progress. Just reset so the player can retry.
            PlayLayer::resetLevel();
            return;
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        bot::BotController::get().onLeaveLevel();
        PlayLayer::onQuit();
    }
};

static void setLabelText(cocos2d::CCNode* btn, const char* text) {
    if (!btn) return;
    for (auto child : CCArrayExt<cocos2d::CCNode*>(btn->getChildren())) {
        if (auto label = typeinfo_cast<CCLabelBMFont*>(child)) {
            label->setString(text);
            return;
        }
    }
}

class $modify(PAUSEBot, PauseLayer) {
    struct Fields {
        CCMenuItem* m_botBtn = nullptr;
        CCMenuItem* m_safeBtn = nullptr;
        CCMenuItem* m_lookBtn = nullptr;
    };

    void refreshLabels() {
        auto& bc = bot::BotController::get();
        setLabelText(m_fields->m_botBtn, bc.enabled() ? "BOT: ON" : "BOT: OFF");
        setLabelText(m_fields->m_safeBtn, bc.safeMode() ? "SAFE: ON" : "SAFE: OFF");
        auto text = CCString::createWithFormat("LOOK: %d", (int)bc.lookaheadBlocks())->getCString();
        setLabelText(m_fields->m_lookBtn, text);
    }

    void onBotToggle(CCObject*) {
        bot::BotController::get().toggleEnabled();
        refreshLabels();
    }
    void onSafeToggle(CCObject*) {
        bot::BotController::get().toggleSafeMode();
        refreshLabels();
    }
    void onLookToggle(CCObject*) {
        bot::BotController::get().cycleLookahead();
        refreshLabels();
    }

    void customSetup() override {
        PauseLayer::customSetup();

        auto menu = cocos2d::CCMenu::create();
        menu->setPosition(0.0f, 0.0f);

        auto makeBtn = [&](const char* label, cocos2d::SEL_MenuHandler sel, CCPoint pos) {
            auto spr = cocos2d::CCSprite::createWithSpriteFrameName("GJ_button_01.png");
            spr->setScale(1.1f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
            auto text = CCLabelBMFont::create(label, "bigFont.fnt");
            text->setScale(0.45f);
            btn->addChild(text);
            btn->setPosition(pos);
            menu->addChild(btn);
            return btn;
        };

        m_fields->m_botBtn = makeBtn("BOT: OFF", menu_selector(PAUSEBot::onBotToggle), {135.0f, 105.0f});
        m_fields->m_safeBtn = makeBtn("SAFE: ON", menu_selector(PAUSEBot::onSafeToggle), {135.0f, 75.0f});
        m_fields->m_lookBtn = makeBtn("LOOK: 5", menu_selector(PAUSEBot::onLookToggle), {135.0f, 45.0f});

        this->addChild(menu, 20);
        refreshLabels();
    }
};