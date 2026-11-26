#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include "BotMenuPopup.hpp"
#include "LiveBot.hpp"
#include "MacroManager.hpp"

#include <algorithm>
#include <string>

using namespace geode::prelude;

namespace {
    PlayLayer* s_currentPlayLayer = nullptr;

    int macroButtonCode(PlayerButton button) {
        switch (button) {
            case PlayerButton::Jump: return 1;
            case PlayerButton::Left: return 2;
            case PlayerButton::Right: return 3;
            default: return 0;
        }
    }

    void showHud(CCLabelBMFont*& label, CCLayer* parent, PlayLayer* layer) {
        auto& manager = MacroManager::get();
        if (!label && parent) {
            label = CCLabelBMFont::create("BOT OFF", "bigFont.fnt");
            if (label) {
                label->setScale(0.32f);
                label->setAnchorPoint({ 0.f, 1.f });
                auto const win = CCDirector::get()->getWinSize();
                label->setPosition({ 8.f, win.height - 8.f });
                label->setColor({ 130, 255, 150 });
                label->setZOrder(1000);
                label->setID("livebot-status-hud");
                parent->addChild(label);
            }
        }
        if (!label) return;
        auto const enabled = manager.mode() != BotMode::Off && manager.hudEnabled();
        label->setVisible(enabled);
        if (enabled && layer) {
            auto const progress = static_cast<int>(std::clamp(layer->getCurrentPercent(), 0.f, 100.f));
            label->setString(fmt::format("{}  {}%  t{}", MacroManager::modeName(manager.mode()), progress, manager.tick()).c_str());
        }
    }

}

class $modify(LiveBotPlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* hud = nullptr;
        CCMenu* floatingMenu = nullptr;
    };

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        s_currentPlayLayer = this;
        auto const id = level ? static_cast<int>(level->m_levelID) : 0;
        auto const name = level ? std::string(level->m_levelName.c_str()) : std::string("Unknown Level");
        MacroManager::get().beginLevel(this, id, name);
        LiveBot::get().reset(this);
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        LiveBot::get().update(this, dt);
        showHud(m_fields->hud, m_uiLayer, this);
        auto& manager = MacroManager::get();
        if (!m_fields->floatingMenu && m_uiLayer) {
            auto* text = CCLabelBMFont::create("LB", "bigFont.fnt");
            auto* sprite = text
                ? CircleButtonSprite::create(text, CircleBaseColor::Green, CircleBaseSize::Small)
                : nullptr;
            auto* menu = CCMenu::create();
            auto* item = sprite
                ? CCMenuItemSpriteExtra::create(sprite, this, menu_selector(LiveBotPlayLayer::onLiveBotButton))
                : nullptr;
            if (menu && item) {
                menu->addChild(item);
                auto const win = CCDirector::get()->getWinSize();
                menu->setPosition({ win.width - 32.f, win.height - 34.f });
                menu->setZOrder(1001);
                menu->setID("livebot-floating-menu");
                m_uiLayer->addChild(menu);
                m_fields->floatingMenu = menu;
            }
        }
        if (m_fields->floatingMenu) {
            m_fields->floatingMenu->setVisible(manager.floatingButtonEnabled());
        }
    }

    CheckpointObject* markCheckpoint() {
        auto* checkpoint = PlayLayer::markCheckpoint();
        if (checkpoint) MacroManager::get().onCheckpointPlaced();
        return checkpoint;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto const hasCheckpoint = m_isPracticeMode && getLastCheckpoint() != nullptr;
        MacroManager::get().onLevelReset(hasCheckpoint);
        LiveBot::get().reset(this);
    }

    void levelComplete() {
        MacroManager::get().saveOnLevelComplete();
        PlayLayer::levelComplete();
    }

    void onQuit() {
        LiveBot::get().releaseAll(this);
        MacroManager::get().endLevel(this);
        if (s_currentPlayLayer == this) s_currentPlayLayer = nullptr;
        PlayLayer::onQuit();
    }

    void onLiveBotButton(CCObject*) {
        auto& manager = MacroManager::get();
        if (manager.mode() == BotMode::LiveBot) {
            LiveBot::get().releaseAll(this);
            manager.setMode(BotMode::Off);
        } else {
            manager.setMode(BotMode::LiveBot);
            LiveBot::get().reset(this);
        }
    }
};

class $modify(LiveBotPlayerObject, PlayerObject) {
    void update(float dt) {
        if (s_currentPlayLayer && this == s_currentPlayLayer->m_player1) {
            MacroManager::get().onPhysicsStep(s_currentPlayLayer,
                s_currentPlayLayer->m_player1, s_currentPlayLayer->m_player2, dt);
        }
        PlayerObject::update(dt);
    }

    bool pushButton(PlayerButton button) {
        auto& manager = MacroManager::get();
        if (manager.shouldBlockManualInput()) return false;
        manager.captureButton(this, macroButtonCode(button), true);
        return PlayerObject::pushButton(button);
    }

    bool releaseButton(PlayerButton button) {
        auto& manager = MacroManager::get();
        if (manager.shouldBlockManualInput()) return false;
        manager.captureButton(this, macroButtonCode(button), false);
        return PlayerObject::releaseButton(button);
    }
};

class $modify(LiveBotPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto* sprite = ButtonSprite::create("Bot & Macros", "goldFont.fnt", "GJ_button_01.png", 0.7f);
        if (!sprite) return;
        sprite->setScale(0.48f);
        auto* button = CCMenuItemSpriteExtra::create(sprite, this,
            menu_selector(LiveBotPauseLayer::onOpenBotStudio));
        if (!button) return;
        button->setID("livebot-open-studio");
        if (auto* menu = this->getChildByID("right-button-menu")) {
            menu->addChild(button);
            menu->updateLayout();
        } else {
            auto* menu = CCMenu::create();
            menu->addChild(button);
            auto const win = CCDirector::get()->getWinSize();
            menu->setPosition({ win.width - 64.f, win.height - 34.f });
            this->addChild(menu, 100);
        }
    }

    void onOpenBotStudio(CCObject*) {
        BotMenuPopup::open();
    }
};
