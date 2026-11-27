#include "LiveBot.hpp"

#include "MacroManager.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace geode::prelude;

namespace {
    constexpr float kMinLookAhead = 100.f;
    constexpr float kMaxLookAhead = 480.f;

    float leftOf(CCRect const& rect) { return rect.origin.x; }
    float rightOf(CCRect const& rect) { return rect.origin.x + rect.size.width; }
    float bottomOf(CCRect const& rect) { return rect.origin.y; }
    float topOf(CCRect const& rect) { return rect.origin.y + rect.size.height; }
    float centerY(CCRect const& rect) { return rect.origin.y + rect.size.height * 0.5f; }

    bool overlaps(CCRect const& a, CCRect const& b) {
        return a.intersectsRect(b);
    }

    bool isVehicleFlying(PlayerObject* player) {
        return player->m_isShip || player->m_isBird || player->m_isDart || player->m_isSwing;
    }

    int modeSignature(PlayerObject* player) {
        if (player->m_isShip) return 1;
        if (player->m_isBird) return 2;
        if (player->m_isDart) return 3;
        if (player->m_isBall) return 4;
        if (player->m_isSpider) return 5;
        if (player->m_isRobot) return 6;
        if (player->m_isSwing) return 7;
        return 0;
    }
}

LiveBot& LiveBot::get() {
    static LiveBot bot;
    return bot;
}

bool LiveBot::isOrb(GameObject* object) const {
    if (!object) return false;
    auto const type = object->m_objectType;
    return type == GameObjectType::YellowJumpRing
        || type == GameObjectType::PinkJumpRing
        || type == GameObjectType::RedJumpRing
        || type == GameObjectType::GravityRing
        || type == GameObjectType::GreenRing
        || type == GameObjectType::DropRing
        || type == GameObjectType::SpiderOrb
        || type == GameObjectType::CustomRing
        || type == GameObjectType::DashRing
        || type == GameObjectType::GravityDashRing
        || type == GameObjectType::TeleportOrb;
}

bool LiveBot::isPad(GameObject* object) const {
    if (!object) return false;
    auto const type = object->m_objectType;
    return type == GameObjectType::YellowJumpPad
        || type == GameObjectType::PinkJumpPad
        || type == GameObjectType::RedJumpPad
        || type == GameObjectType::GravityPad
        || type == GameObjectType::SpiderPad;
}

bool LiveBot::isBlockingObject(GameObject* object) const {
    if (!object || isOrb(object) || isPad(object)) return false;
    auto const type = object->m_objectType;
    return type == GameObjectType::Hazard
        || type == GameObjectType::AnimatedHazard
        || type == GameObjectType::Solid
        || type == GameObjectType::Breakable
        || type == GameObjectType::Slope
        || type == GameObjectType::CollisionObject;
}

float LiveBot::lookAhead(PlayerObject* player) const {
    auto const speed = std::fabs(player->m_playerSpeed);
    auto const gravityMultiplier = std::sqrt(std::clamp(std::fabs(player->m_gravityMod), 0.4f, 2.5f))
        * (player->m_isUpsideDown ? 1.08f : 1.f);
    return std::clamp(speed * 0.62f * gravityMultiplier
        * MacroManager::get().lookaheadMultiplier(), kMinLookAhead, kMaxLookAhead);
}

void LiveBot::setButton(PlayerObject* player, int playerIndex, int button, bool down) {
    auto& state = m_players[static_cast<size_t>(playerIndex)];
    auto const buttonIndex = static_cast<size_t>(button - 1);
    if (buttonIndex >= 3 || state.held[buttonIndex] == down) return;
    if (!player) {
        if (!down) state.held[buttonIndex] = false;
        return;
    }
    MacroManager::get().sendButton(player, button, down, InputOrigin::LiveBot);
    state.held[buttonIndex] = down;
}

void LiveBot::pulse(PlayerObject* player, int playerIndex, int frames) {
    if (!player) return;
    auto& state = m_players[static_cast<size_t>(playerIndex)];
    setButton(player, playerIndex, 1, true);
    state.pulseFrames = std::max(state.pulseFrames, frames);
}

bool LiveBot::triggerOrb(PlayerObject* player, int playerIndex, std::vector<GameObject*> const& objects) {
    auto& state = m_players[static_cast<size_t>(playerIndex)];
    auto const playerRect = player->getObjectRect();

    if (state.lastOrb) {
        bool exists = false;
        for (auto* object : objects) {
            if (object == state.lastOrb) {
                exists = true;
                break;
            }
        }
        if (!exists || !overlaps(playerRect, state.lastOrb->getObjectRect())) state.lastOrb = nullptr;
    }

    for (auto* object : objects) {
        if (!isOrb(object) || object == state.lastOrb) continue;
        if (!overlaps(playerRect, object->getObjectRect())) continue;
        if (state.held[0]) setButton(player, playerIndex, 1, false);
        pulse(player, playerIndex, 1);
        state.lastOrb = object;
        state.cooldown = 5;
        return true;
    }
    return false;
}

float LiveBot::platformEdgeDistance(PlayerObject* player, float range,
    std::vector<GameObject*> const& objects) const {
    auto const playerRect = player->getObjectRect();
    auto const centerX = leftOf(playerRect) + playerRect.size.width * 0.5f;
    auto const right = rightOf(playerRect);
    auto const surfaceY = player->m_isUpsideDown ? topOf(playerRect) : bottomOf(playerRect);
    constexpr float surfaceTolerance = 11.f;
    constexpr float seamTolerance = 8.f;
    std::vector<std::pair<float, float>> platforms;

    for (auto* object : objects) {
        if (!object || (object->m_objectType != GameObjectType::Solid
            && object->m_objectType != GameObjectType::Breakable)) continue;
        auto const rect = object->getObjectRect();
        if (rightOf(rect) < centerX || leftOf(rect) > right + range + 12.f) continue;
        auto const objectSurface = player->m_isUpsideDown ? bottomOf(rect) : topOf(rect);
        if (std::fabs(objectSurface - surfaceY) <= surfaceTolerance) {
            platforms.emplace_back(leftOf(rect), rightOf(rect));
        }
    }

    std::sort(platforms.begin(), platforms.end());
    float supportedUntil = -1.f;
    for (auto const& platform : platforms) {
        if (platform.first <= centerX && platform.second >= centerX) {
            supportedUntil = std::max(supportedUntil, platform.second);
        }
    }
    if (supportedUntil < centerX) return range + 1.f;

    for (auto const& platform : platforms) {
        if (platform.first <= supportedUntil + seamTolerance && platform.second > supportedUntil) {
            supportedUntil = std::max(supportedUntil, platform.second);
        }
    }
    if (supportedUntil >= right + range) return range + 1.f;
    return std::max(0.f, supportedUntil - right);
}

GameObject* LiveBot::nearestGroundThreat(PlayerObject* player, float range, float& distance,
    std::vector<GameObject*> const& objects) const {
    auto const rect = player->getObjectRect();
    auto const playerRight = rightOf(rect);
    auto const height = rect.size.height;
    auto const band = player->m_isUpsideDown
        ? CCRect(leftOf(rect), bottomOf(rect) - height * 1.5f, rect.size.width + range, height * 2.5f)
        : CCRect(leftOf(rect), bottomOf(rect) - height * 0.5f, rect.size.width + range, height * 2.5f);
    GameObject* nearest = nullptr;
    distance = range + 1.f;

    for (auto* object : objects) {
        if (!isBlockingObject(object)) continue;
        auto const objectRect = object->getObjectRect();
        if (rightOf(objectRect) <= playerRight || leftOf(objectRect) > playerRight + range) continue;
        if (!overlaps(band, objectRect)) continue;
        auto const objectDistance = std::max(0.f, leftOf(objectRect) - playerRight);
        if (objectDistance < distance) {
            nearest = object;
            distance = objectDistance;
        }
    }
    return nearest;
}

bool LiveBot::safeOppositeSurface(PlayerObject* player, float range,
    std::vector<GameObject*> const& objects) const {
    auto const playerRect = player->getObjectRect();
    auto const playerRight = rightOf(playerRect);
    constexpr float maxGap = 180.f;
    for (auto* object : objects) {
        if (!object || (object->m_objectType != GameObjectType::Solid
            && object->m_objectType != GameObjectType::Breakable)) continue;
        auto const rect = object->getObjectRect();
        if (rightOf(rect) <= playerRight || leftOf(rect) > playerRight + range) continue;
        auto const gap = player->m_isUpsideDown
            ? bottomOf(playerRect) - topOf(rect)
            : bottomOf(rect) - topOf(playerRect);
        if (gap < 0.f || gap > maxGap) continue;

        auto const landingY = player->m_isUpsideDown
            ? topOf(rect) + playerRect.size.height * 0.5f
            : bottomOf(rect) - playerRect.size.height * 0.5f;
        auto const landing = CCRect(leftOf(playerRect), landingY - playerRect.size.height * 0.5f,
            playerRect.size.width, playerRect.size.height);
        bool safe = true;
        for (auto* other : objects) {
            if (other == object || !isBlockingObject(other)) continue;
            auto const otherRect = other->getObjectRect();
            if (leftOf(otherRect) > playerRight + range || rightOf(otherRect) < playerRight) continue;
            if (overlaps(landing, otherRect)) {
                safe = false;
                break;
            }
        }
        if (safe) return true;
    }
    return false;
}

float LiveBot::chooseFlightTarget(PlayerObject* player, float range, PlayerState& state,
    std::vector<GameObject*> const& objects) const {
    auto const playerRect = player->getObjectRect();
    auto const playerRight = rightOf(playerRect);
    auto const currentY = centerY(playerRect);
    float planeX = playerRight + range;
    bool found = false;

    for (auto* object : objects) {
        if (!isBlockingObject(object)) continue;
        auto const rect = object->getObjectRect();
        if (rightOf(rect) <= playerRight || leftOf(rect) > playerRight + range) continue;
        auto const candidate = std::max(playerRight + 8.f, leftOf(rect) - playerRect.size.width * 0.25f);
        if (!found || candidate < planeX) {
            planeX = candidate;
            found = true;
        }
    }
    if (!found) return state.flightTargetInitialized ? state.flightTargetY : currentY;

    auto const low = currentY - 190.f;
    auto const high = currentY + 190.f;
    std::vector<std::pair<float, float>> blocked;
    for (auto* object : objects) {
        if (!isBlockingObject(object)) continue;
        auto const rect = object->getObjectRect();
        if (planeX < leftOf(rect) - playerRect.size.width * 0.5f
            || planeX > rightOf(rect) + playerRect.size.width * 0.5f) continue;
        auto const bottom = std::max(low, bottomOf(rect) - playerRect.size.height * 0.5f - 8.f);
        auto const top = std::min(high, topOf(rect) + playerRect.size.height * 0.5f + 8.f);
        if (top > bottom) blocked.emplace_back(bottom, top);
    }
    std::sort(blocked.begin(), blocked.end());

    float cursor = low;
    float bestCenter = currentY;
    float bestScore = -1.f;
    auto considerGap = [&](float end) {
        auto const gap = end - cursor;
        if (gap < playerRect.size.height + 12.f) return;
        auto const center = (cursor + end) * 0.5f;
        auto const score = gap - std::fabs(center - currentY) * 0.4f;
        if (score > bestScore) {
            bestScore = score;
            bestCenter = center;
        }
    };
    for (auto const& interval : blocked) {
        if (interval.first > cursor) considerGap(interval.first);
        cursor = std::max(cursor, interval.second);
    }
    if (cursor < high) considerGap(high);
    if (bestScore > 0.f) {
        state.flightTargetY = bestCenter;
        state.flightTargetInitialized = true;
    }
    return state.flightTargetInitialized ? state.flightTargetY : currentY;
}

bool LiveBot::rolloutSurvives(PlayerObject* player, std::vector<GameObject*> const& objects,
    bool press, int holdFrames, float gravity, float dt, int delayFrames,
    int substepsPerFrame) const {
    auto const initialRect = player->getObjectRect();
    auto const playerHeight = initialRect.size.height;
    auto const gravityDirection = player->m_isUpsideDown ? 1.f : -1.f;
    auto const gravityScale = std::clamp(std::fabs(player->m_gravityMod), 0.4f, 2.5f);
    auto const g = gravityDirection * gravity * gravityScale;
    // dt is the real frame duration. Virtual substeps improve collision sampling
    // without changing the game's rendering or physics frame rate.
    auto const frameDt = std::clamp(dt, 1.f / 240.f, 1.f / 30.f);
    auto const substeps = std::clamp(substepsPerFrame, 1, 16);
    constexpr int horizonFrames = 24;
    auto const speed = std::max(80.f, std::fabs(player->m_playerSpeed));
    auto y = centerY(initialRect);
    auto vy = player->m_yVelocity;
    auto const modeShip = player->m_isShip;
    auto const modeUfo = player->m_isBird;
    auto const modeWave = player->m_isDart;
    auto const modeBall = player->m_isBall;
    auto const modeSpider = player->m_isSpider;
    bool jumped = false;
    auto const startsGrounded = player->m_isOnGround;
    auto const impulse = (player->m_isRobot ? 500.f : 455.f)
        * std::clamp(player->m_vehicleSize, 0.5f, 1.5f);

    for (int step = 0; step < horizonFrames * substeps; ++step) {
        auto const frameIndex = step / substeps;
        auto const stepDt = frameDt / static_cast<float>(substeps);
        auto const elapsed = static_cast<float>(step + 1) * stepDt;
        auto const inputActive = press && frameIndex >= delayFrames
            && frameIndex < delayFrames + std::max(1, holdFrames);

        if (press && !jumped && frameIndex == delayFrames) {
            if (startsGrounded && !modeShip && !modeUfo && !modeWave && !modeBall && !modeSpider) {
                vy = -gravityDirection * impulse;
                jumped = true;
            } else if (modeUfo) {
                vy = -gravityDirection * 390.f;
                jumped = true;
            } else if (modeBall) {
                vy = -gravityDirection * vy;
                jumped = true;
            } else if (modeSpider) {
                vy = -gravityDirection * 350.f;
                jumped = true;
            }
        }

        if (modeWave) {
            vy = (inputActive ? -gravityDirection : gravityDirection) * 420.f;
        } else if (modeShip && inputActive) {
            vy += -gravityDirection * gravity * 2.4f * stepDt;
        } else if (player->m_isRobot && inputActive && jumped) {
            vy += -gravityDirection * gravity * 0.55f * stepDt;
        }

        // A grounded player remains supported until the simulated jump begins.
        // This avoids the old rollout incorrectly dropping the player at t=0.
        if (jumped || !startsGrounded) {
            y += vy * stepDt + 0.5f * g * stepDt * stepDt;
            vy += g * stepDt;
        }

        auto const xShift = speed * elapsed;
        auto const yShift = y - centerY(initialRect);
        auto const predicted = CCRect(initialRect.origin.x + xShift,
            initialRect.origin.y + yShift, initialRect.size.width, initialRect.size.height);

        for (auto* object : objects) {
            if (!isBlockingObject(object)) continue;
            if (overlaps(predicted, object->getObjectRect())) return false;
        }

        if (!player->m_isUpsideDown && y < centerY(initialRect) - playerHeight * 5.f) return false;
        if (player->m_isUpsideDown && y > centerY(initialRect) + playerHeight * 5.f) return false;
    }
    return true;
}

void LiveBot::updatePlayer(PlayerObject* player, int playerIndex, float dt,
    std::vector<GameObject*> const& layerObjects) {
    if (!player) return;
    auto& state = m_players[static_cast<size_t>(playerIndex)];
    auto const signature = modeSignature(player);
    if (state.lastMode != signature || state.lastUpsideDown != player->m_isUpsideDown) {
        setButton(player, playerIndex, 1, false);
        state.pulseFrames = 0;
        state.cooldown = 0;
        state.flightTargetInitialized = false;
        state.plannedJumpDelay = -1;
        state.plannedJumpHold = 1;
        state.lastOrb = nullptr;
        state.lastMode = signature;
        state.lastUpsideDown = player->m_isUpsideDown;
    }
    auto& mod = *Mod::get();
    auto const levelID = MacroManager::get().levelID();
    if (state.learningLevelID != levelID) {
        state.learningLevelID = levelID;
        auto const key = fmt::format("livebot-learning-{}-{}", levelID, playerIndex);
        state.learnedTriggerBias = std::clamp(mod.getSavedValue<float>(key + "-bias", 0.f), -30.f, 30.f);
        state.learnedFailures = std::max(0, mod.getSavedValue<int>(key + "-failures", 0));
        state.hasRecentDecision = false;
        state.deathHandled = false;
    }

    if (player->m_isDead) {
        if (!state.deathHandled) {
            state.deathHandled = true;
            if (state.hasRecentDecision && MacroManager::get().tick() - state.lastDecisionTick <= 90) {
                // A death shortly after a jump suggests the bot acted too early;
                // a death after declining a nearby jump suggests it should react sooner.
                auto const adjustment = state.lastDecisionWasJump ? -2.5f : 3.5f;
                state.learnedTriggerBias = std::clamp(state.learnedTriggerBias + adjustment, -30.f, 30.f);
                state.learnedFailures = std::min(state.learnedFailures + 1, 100000);
                auto const key = fmt::format("livebot-learning-{}-{}", levelID, playerIndex);
                mod.setSavedValue(key + "-bias", state.learnedTriggerBias);
                mod.setSavedValue(key + "-failures", state.learnedFailures);
            }
        }
        for (int button = 1; button <= 3; ++button) setButton(player, playerIndex, button, false);
        state.pulseFrames = 0;
        return;
    }
    state.deathHandled = false;

    if (state.cooldown > 0) --state.cooldown;
    if (state.pulseFrames > 0 && --state.pulseFrames == 0) setButton(player, playerIndex, 1, false);

    auto const velocityY = player->m_yVelocity;
    if (state.hasPrevious && dt > 0.0001f) {
        auto const measuredGravity = std::fabs(velocityY - state.lastVelocityY) / dt;
        if (std::isfinite(measuredGravity) && measuredGravity > 250.f && measuredGravity < 8000.f) {
            state.gravityEstimate = state.gravityEstimate * 0.82f + measuredGravity * 0.18f;
        }
    }
    state.lastVelocityY = velocityY;
    state.hasPrevious = true;

    auto const range = lookAhead(player);
    auto const playerRect = player->getObjectRect();
    auto const searchLeft = leftOf(playerRect) - 30.f;
    auto const searchRight = rightOf(playerRect) + range;
    std::vector<GameObject*> nearby;
    nearby.reserve(48);
    for (auto* object : layerObjects) {
        if (!object) continue;
        auto const rect = object->getObjectRect();
        if (rightOf(rect) < searchLeft || leftOf(rect) > searchRight) continue;
        nearby.push_back(object);
    }

    if (triggerOrb(player, playerIndex, nearby)) return;

    float threatDistance = range + 1.f;
    auto* threat = nearestGroundThreat(player, range, threatDistance, nearby);
    auto const edgeDistance = platformEdgeDistance(player, range, nearby);
    auto const baseTriggerDistance = std::clamp(std::fabs(player->m_playerSpeed) * 0.19f, 26.f, 100.f);
    auto const triggerDistance = std::clamp(baseTriggerDistance + state.learnedTriggerBias, 18.f, 140.f);

    if (isVehicleFlying(player)) {
        if (!state.flightTargetInitialized) {
            state.flightTargetY = centerY(playerRect);
            state.flightTargetInitialized = true;
        }
        auto const targetY = chooseFlightTarget(player, range, state, nearby);
        auto const signedError = (targetY - centerY(playerRect)) * (player->m_isUpsideDown ? -1.f : 1.f);
        auto const needsLift = signedError > 18.f;
        if (player->m_isBird || player->m_isSwing) {
            if (needsLift && state.cooldown == 0) {
                pulse(player, playerIndex, 1);
                state.cooldown = 7;
            }
        } else {
            setButton(player, playerIndex, 1, needsLift);
        }
        return;
    }

    if (player->m_isBall || player->m_isSpider) {
        auto const surfaceThreat = player->m_isOnGround
            && ((threat && threatDistance <= triggerDistance) || edgeDistance <= triggerDistance);
        if (surfaceThreat && state.cooldown == 0 && safeOppositeSurface(player, range, nearby)) {
            pulse(player, playerIndex, 1);
            state.cooldown = 14;
        }
        return;
    }

    if (!player->m_isOnGround) {
        state.plannedJumpDelay = -1;
        return;
    }
    if (state.cooldown > 0) {
        state.plannedJumpDelay = -1;
        return;
    }
    auto const obstacleClose = threat && threatDistance <= triggerDistance;
    auto const edgeClose = edgeDistance <= triggerDistance;
    if (!obstacleClose && !edgeClose) {
        state.plannedJumpDelay = -1;
        return;
    }

    auto const obstacleRect = threat ? threat->getObjectRect() : CCRect(0.f, 0.f, 0.f, 0.f);
    auto const obstacleHeight = threat ? std::max(0.f, topOf(obstacleRect) - bottomOf(playerRect)) : 0.f;
    auto const highRobotJump = player->m_isRobot
        && obstacleClose && obstacleHeight > playerRect.size.height * 1.25f;
    auto const jumpHold = highRobotJump ? 7 : 1;
    auto const imminentEdge = edgeClose
        && edgeDistance <= std::max(8.f, triggerDistance * 0.25f);

    auto const decisionDistance = threat ? threatDistance : edgeDistance;
    // Adaptive virtual simulation tiers: 60/120/240/480/960 samples per
    // rendered frame. Higher precision is reserved for close, difficult hazards.
    int simulationSubsteps = 1;
    if (decisionDistance <= 18.f) simulationSubsteps = 16;
    else if (decisionDistance <= 30.f) simulationSubsteps = 8;
    else if (decisionDistance <= 48.f) simulationSubsteps = 4;
    else if (decisionDistance <= 80.f) simulationSubsteps = 2;

    auto const noInputSurvives = rolloutSurvives(player, nearby, false, 0,
        state.gravityEstimate, dt, 0, simulationSubsteps);

    if (noInputSurvives && !imminentEdge) {
        state.plannedJumpDelay = -1;
        state.plannedJumpHold = 1;
        return;
    }

    // Search a short sequence of candidate jump times. The farthest safe delay
    // avoids jumping prematurely; the countdown is kept monotonic between frames.
    auto const frameDt = std::clamp(dt, 1.f / 240.f, 1.f / 30.f);
    auto const speed = std::max(80.f, std::fabs(player->m_playerSpeed));
    auto const travelFrames = static_cast<int>(decisionDistance / (speed * frameDt));
    auto const maxDelay = std::clamp(travelFrames - 2, 0, 8);
    int bestDelay = -1;
    for (int delay = 0; delay <= maxDelay; ++delay) {
        if (rolloutSurvives(player, nearby, true, jumpHold, state.gravityEstimate,
                dt, delay, simulationSubsteps)) {
            bestDelay = delay;
        }
    }

    if (bestDelay >= 0) {
        if (state.plannedJumpDelay < 0) {
            state.plannedJumpDelay = bestDelay;
            state.plannedJumpHold = jumpHold;
        } else {
            state.plannedJumpDelay = std::min(state.plannedJumpDelay, bestDelay);
            state.plannedJumpHold = jumpHold;
        }

        if (state.plannedJumpDelay <= 0) {
            pulse(player, playerIndex, state.plannedJumpHold);
            state.plannedJumpDelay = -1;
            state.lastDecisionWasJump = true;
            state.lastDecisionTick = MacroManager::get().tick();
            state.lastThreatDistance = decisionDistance;
            state.hasRecentDecision = true;
            state.cooldown = highRobotJump ? 16 : 9;
        } else {
            --state.plannedJumpDelay;
        }
    } else {
        state.plannedJumpDelay = -1;
        // Last-resort behavior: if a collision is predicted and every delayed
        // candidate fails, try the immediate jump rather than remaining idle.
        if (!noInputSurvives && obstacleClose) {
            pulse(player, playerIndex, jumpHold);
            state.lastDecisionWasJump = true;
            state.lastDecisionTick = MacroManager::get().tick();
            state.lastThreatDistance = decisionDistance;
            state.hasRecentDecision = true;
            state.cooldown = highRobotJump ? 16 : 9;
        } else {
            state.lastDecisionWasJump = false;
            state.lastDecisionTick = MacroManager::get().tick();
            state.lastThreatDistance = decisionDistance;
            state.hasRecentDecision = true;
        }
    }
}

void LiveBot::update(PlayLayer* layer, float dt) {
    auto& manager = MacroManager::get();
    if (m_layer != layer) reset(layer);
    if (manager.mode() != BotMode::LiveBot || !layer || !layer->m_player1) {
        releaseAll(layer);
        return;
    }

    std::vector<GameObject*> layerObjects;
    if (layer->m_objects) {
        auto const count = layer->m_objects->count();
        layerObjects.reserve(count);
        for (unsigned int i = 0; i < count; ++i) {
            if (auto* object = static_cast<GameObject*>(layer->m_objects->objectAtIndex(i))) {
                layerObjects.push_back(object);
            }
        }
    }
    updatePlayer(layer->m_player1, 0, dt, layerObjects);
    auto const dualMode = layer->m_player2 && layer->m_player2->isVisible();
    if (dualMode) updatePlayer(layer->m_player2, 1, dt, layerObjects);
    else {
        for (int button = 1; button <= 3; ++button) setButton(layer->m_player2, 1, button, false);
        m_players[1].pulseFrames = 0;
    }
}

void LiveBot::reset(PlayLayer* layer) {
    m_layer = layer;
    m_players = {};
    for (auto& player : m_players) player.gravityEstimate = 1800.f;
}

void LiveBot::releaseAll(PlayLayer* layer) {
    if (!layer) return;
    for (int playerIndex = 0; playerIndex < 2; ++playerIndex) {
        auto* player = playerIndex == 0 ? layer->m_player1 : layer->m_player2;
        for (int button = 1; button <= 3; ++button) setButton(player, playerIndex, button, false);
        m_players[static_cast<size_t>(playerIndex)].pulseFrames = 0;
    }
}
