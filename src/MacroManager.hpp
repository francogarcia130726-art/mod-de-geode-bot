#pragma once

#include <Geode/Geode.hpp>
#include <asp/fs/Path.hpp>

#include <string>
#include <vector>

class PlayLayer;
class PlayerObject;

enum class BotMode {
    Off,
    LiveBot,
    Record,
    Playback,
};

enum class InputOrigin {
    Manual,
    LiveBot,
    Playback,
};

struct MacroAction {
    int tick = 0;
    double time = 0.0;
    int player = 1;
    int button = 1;
    bool down = false;
    float posX = 0.f;
    float posY = 0.f;
    bool upsideDown = false;
    int checkpointId = 0;
};

struct MacroSummary {
    std::string name;
    std::string levelName;
    int levelID = 0;
    size_t actionCount = 0;
    asp::fs::Path path;
};

class MacroManager {
public:
    static MacroManager& get();

    BotMode mode() const;
    void setMode(BotMode mode);
    static char const* modeName(BotMode mode);

    int tick() const;
    double time() const;
    size_t actionCount() const;
    std::vector<MacroAction> const& actions() const;
    std::string const& levelName() const;
    int levelID() const;
    PlayLayer* activeLayer() const;

    void beginLevel(PlayLayer* layer, int levelID, std::string levelName);
    void endLevel(PlayLayer* layer);
    void onPhysicsStep(PlayLayer* layer, PlayerObject* player1, PlayerObject* player2, float dt);
    void captureButton(PlayerObject* player, int button, bool down);
    void sendButton(PlayerObject* player, int button, bool down, InputOrigin origin);
    bool shouldBlockManualInput() const;
    InputOrigin inputOrigin() const;

    void onCheckpointPlaced();
    void onLevelReset(bool checkpointRestore);

    bool autoRecordFromLiveBot() const;
    void setAutoRecordFromLiveBot(bool enabled);
    bool blockManualPlayback() const;
    void setBlockManualPlayback(bool enabled);
    bool floatingButtonEnabled() const;
    void setFloatingButtonEnabled(bool enabled);
    bool hudEnabled() const;
    void setHudEnabled(bool enabled);
    float lookaheadMultiplier() const;
    void setLookaheadMultiplier(float value);
    bool safeMode() const;
    void activateSafeModeForAttempt();

    void clearMacro();
    bool saveMacro(std::string const& name, std::string& error);
    bool loadMacro(asp::fs::Path const& path, std::string& error);
    bool loadMacroByName(std::string const& name, std::string& error);
    bool importJson(std::string const& json, std::string& error);
    bool importFile(asp::fs::Path const& path, std::string& error);
    bool exportMacro(std::string const& name, asp::fs::Path& outputPath, std::string& error);
    bool deleteMacro(asp::fs::Path const& path, std::string& error);
    std::vector<MacroSummary> listMacros() const;
    asp::fs::Path macroDirectory() const;

    void saveOnLevelComplete();

private:
    MacroManager();

    struct CheckpointMark {
        size_t actionCount = 0;
        size_t playbackCursor = 0;
        int tick = 0;
        double time = 0.0;
        int id = 0;
    };

    asp::fs::Path pathForName(std::string const& name) const;
    std::string buildJson(std::string const& name) const;
    bool parseJson(std::string const& json, std::string& error);
    void applyPlayback(PlayerObject* player1, PlayerObject* player2);
    void releasePlaybackButtons(PlayerObject* player1, PlayerObject* player2);
    void resetAttempt(bool clearRecordedActions);

    BotMode m_mode = BotMode::Off;
    InputOrigin m_inputOrigin = InputOrigin::Manual;
    std::vector<MacroAction> m_actions;
    std::vector<CheckpointMark> m_checkpoints;
    int m_tick = 0;
    double m_time = 0.0;
    int m_checkpointId = 0;
    size_t m_playbackCursor = 0;
    int m_desyncWaitTicks = 0;
    int m_levelID = 0;
    std::string m_levelName = "Unknown Level";
    bool m_autoRecordFromLiveBot = false;
    bool m_blockManualPlayback = true;
    bool m_floatingButtonEnabled = false;
    bool m_hudEnabled = true;
    float m_lookaheadMultiplier = 1.f;
    bool m_safeModeAttemptActive = false;
    bool m_playbackHeld[2][3] = {};
    PlayerObject* m_lastPlayer1 = nullptr;
    PlayerObject* m_lastPlayer2 = nullptr;
    PlayLayer* m_activeLayer = nullptr;
};
