#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
    bool s_botEnabled = false;
    bool s_releaseJumpNextFrame = false;
    int s_jumpCooldown = 0;
    CCLabelBMFont* s_toggleLabel = nullptr;

    bool isCube(PlayerObject const* player) {
        return player
            && !player->m_isShip
            && !player->m_isBall
            && !player->m_isBird
            && !player->m_isDart
            && !player->m_isRobot
            && !player->m_isSpider;
    }

    bool isHazard(GameObject const* object) {
        return object && (
            object->m_objectType == GameObjectType::Hazard
            || object->m_objectType == GameObjectType::AnimatedHazard
        );
    }

    bool isSolid(GameObject const* object) {
        return object && object->m_objectType == GameObjectType::Solid;
    }

    bool hasGapAhead(PlayLayer* layer, PlayerObject* player, float distance) {
        auto const playerRect = player->getObjectRect();
        auto const playerRight = playerRect.origin.x + playerRect.size.width;
        auto const groundEdge = player->m_isUpsideDown
            ? playerRect.origin.y + playerRect.size.height
            : playerRect.origin.y;
        constexpr float sampleStep = 12.f;
        constexpr float groundTolerance = 9.f;

        auto hasSupportAt = [&](float x) {
            for (auto* object : layer->m_objects) {
                if (!isSolid(object)) continue;
                auto const rect = object->getObjectRect();
                auto const left = rect.origin.x;
                auto const right = left + rect.size.width;
                if (x < left || x > right) continue;

                auto const surfaceY = player->m_isUpsideDown
                    ? rect.origin.y
                    : rect.origin.y + rect.size.height;
                if (std::fabs(surfaceY - groundEdge) <= groundTolerance) return true;
            }
            return false;
        };

        // The built-in level floor is not part of m_objects. Only infer a pit
        // when the player is standing on a solid object that can be observed.
        if (!hasSupportAt(playerRect.origin.x + playerRect.size.width * 0.5f)) {
            return false;
        }

        for (float x = playerRight + 14.f; x <= playerRight + distance; x += sampleStep) {
            if (!hasSupportAt(x)) return true;
        }
        return false;
    }

    bool hasObstacleAhead(PlayLayer* layer, PlayerObject* player, float distance) {
        auto const playerRect = player->getObjectRect();
        auto const left = playerRect.origin.x;
        auto const right = left + playerRect.size.width;
        auto const bottom = playerRect.origin.y;
        auto const top = bottom + playerRect.size.height;
        auto const sweep = CCRect(left, bottom - 5.f, playerRect.size.width + distance, playerRect.size.height + 10.f);

        for (auto* object : layer->m_objects) {
            if (!object || (!isHazard(object) && !isSolid(object))) continue;
            auto const rect = object->getObjectRect();
            auto const objectLeft = rect.origin.x;
            auto const objectRight = objectLeft + rect.size.width;
            if (objectRight <= right || objectLeft > right + distance) continue;

            if (isHazard(object) && sweep.intersectsRect(rect)) return true;

            // Only jump for a solid block that rises into the player's path;
            // flat ground beneath the player is not an obstacle.
            auto const verticalOverlap = rect.origin.y < top - 2.f
                && rect.origin.y + rect.size.height > bottom + 2.f;
            if (isSolid(object) && verticalOverlap) return true;
        }
        return false;
    }

    void releaseBotJump() {
        if (auto* layer = PlayLayer::get()) {
            if (layer->m_player1) {
                layer->m_player1->releaseButton(PlayerButton::Jump);
            }
        }
        s_releaseJumpNextFrame = false;
    }

    void updateToggleLabel() {
        if (s_toggleLabel) {
            s_toggleLabel->setString(s_botEnabled ? "Bot: ON" : "Bot: OFF");
        }
    }
}

class $modify(LiveBotPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (s_releaseJumpNextFrame) releaseBotJump();
        if (s_jumpCooldown > 0) --s_jumpCooldown;

        auto* player = m_player1;
        if (!s_botEnabled || !player || player->m_isDead) {
            if (s_releaseJumpNextFrame) releaseBotJump();
            return;
        }

        if (!isCube(player) || !player->m_isOnGround) return;
        if (s_jumpCooldown > 0) return;

        // Player speed is level-dependent. The clamp prevents both late and
        // excessively early reactions at the speed portals' extremes.
        auto const reactionDistance = std::clamp(
            std::fabs(player->m_playerSpeed) * 0.20f,
            36.f,
            140.f
        );
        if (!hasObstacleAhead(this, player, reactionDistance)
            && !hasGapAhead(this, player, reactionDistance)) {
            return;
        }

        player->pushButton(PlayerButton::Jump);
        s_releaseJumpNextFrame = true;
        s_jumpCooldown = 12;
    }
};

class $modify(LiveBotPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        s_toggleLabel = nullptr;

        auto* label = CCLabelBMFont::create(
            s_botEnabled ? "Bot: ON" : "Bot: OFF",
            "bigFont.fnt"
        );
        if (!label) return;
        label->setScale(0.55f);
        s_toggleLabel = label;

        auto* button = CCMenuItemLabel::create(
            label,
            this,
            menu_selector(LiveBotPauseLayer::onToggleBot)
        );
        if (!button) return;

        auto* menu = CCMenu::create();
        if (!menu) return;
        menu->addChild(button);
        auto const winSize = CCDirector::get()->getWinSize();
        menu->setPosition({ winSize.width * 0.5f, winSize.height * 0.18f });
        menu->setID("livebot-toggle-menu");
        this->addChild(menu, 100);
    }

    void onToggleBot(CCObject*) {
        s_botEnabled = !s_botEnabled;
        updateToggleLabel();
        if (!s_botEnabled) releaseBotJump();
    }
};
