#pragma once

#include <Geode/Geode.hpp>

#include <array>
#include <vector>

class PlayLayer;
class PlayerObject;
class GameObject;

class LiveBot {
public:
    static LiveBot& get();

    void update(PlayLayer* layer, float dt);
    void reset(PlayLayer* layer = nullptr);
    void releaseAll(PlayLayer* layer);

private:
    struct PlayerState {
        bool held[3] = {};
        int cooldown = 0;
        int pulseFrames = 0;
        float gravityEstimate = 1800.f;
        float lastVelocityY = 0.f;
        float flightTargetY = 0.f;
        bool hasPrevious = false;
        bool flightTargetInitialized = false;
        int lastMode = -1;
        bool lastUpsideDown = false;
        GameObject* lastOrb = nullptr;
    };

    bool isBlockingObject(GameObject* object) const;
    bool isOrb(GameObject* object) const;
    bool isPad(GameObject* object) const;
    float lookAhead(PlayerObject* player) const;
    void updatePlayer(PlayerObject* player, int playerIndex, float dt,
        std::vector<GameObject*> const& layerObjects);
    void setButton(PlayerObject* player, int playerIndex, int button, bool down);
    void pulse(PlayerObject* player, int playerIndex, int frames);
    bool triggerOrb(PlayerObject* player, int playerIndex, std::vector<GameObject*> const& objects);
    float platformEdgeDistance(PlayerObject* player, float range, std::vector<GameObject*> const& objects) const;
    GameObject* nearestGroundThreat(PlayerObject* player, float range, float& distance,
        std::vector<GameObject*> const& objects) const;
    bool safeOppositeSurface(PlayerObject* player, float range, std::vector<GameObject*> const& objects) const;
    float chooseFlightTarget(PlayerObject* player, float range, PlayerState& state,
        std::vector<GameObject*> const& objects) const;
    bool rolloutSurvives(PlayerObject* player, std::vector<GameObject*> const& objects,
        bool press, int holdFrames, float gravity, float dt) const;

    std::array<PlayerState, 2> m_players;
    PlayLayer* m_layer = nullptr;
};
