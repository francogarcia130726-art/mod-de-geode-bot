#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/MenuLayer.hpp>

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

    void checkForRestartNotice() {
        static bool shownRestartNotice = false;
        auto* mod = Mod::get();
        if (shownRestartNotice || !mod ||
            mod->getRequestedAction() != ModRequestedAction::Update) {
            return;
        }

        auto* popup = FLAlertLayer::create(
            "Restart required",
            "The new <cy>Live Level Bot</c> update has been downloaded. "
            "Please <cy>restart Geometry Dash</c> to apply it.",
            "OK"
        );
        if (popup) {
            shownRestartNotice = true;
            popup->show();
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

class $modify(LiveBotMenuLayer, MenuLayer) {
    void onEnter() {
        MenuLayer::onEnter();
        checkForRestartNotice();
    }

    void update(float dt) {
        MenuLayer::update(dt);
        checkForRestartNotice();
    }
};

