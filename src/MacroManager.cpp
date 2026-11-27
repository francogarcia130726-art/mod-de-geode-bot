#include "MacroManager.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/general.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstdint>
#include <limits>
#include <utility>

using namespace geode::prelude;

namespace {
    constexpr double kPositionTolerance = 22.0;
    constexpr int kMaxDesyncWait = 12;

    std::string sanitize(std::string const& value) {
        std::string result;
        result.reserve(std::min<size_t>(value.size(), 64));
        for (unsigned char ch : value) {
            if (std::isalnum(ch) || ch == '-' || ch == '_') result.push_back(static_cast<char>(ch));
            else if (ch == ' ' && !result.empty() && result.back() != '_') result.push_back('_');
            if (result.size() >= 64) break;
        }
        while (!result.empty() && result.back() == '_') result.pop_back();
        return result.empty() ? "macro" : result;
    }

    std::string timestampNow() {
        auto const now = std::time(nullptr);
        std::tm utc{};
#if defined(_WIN32)
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        char buffer[32]{};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
        return buffer;
    }

    bool readInt(matjson::Value const& value, char const* key, int& result) {
        auto parsed = value[key].asInt();
        if (!parsed.isOk()) return false;
        auto const number = parsed.unwrap();
        if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) return false;
        result = static_cast<int>(number);
        return true;
    }

    bool readFloat(matjson::Value const& value, char const* key, float& result) {
        auto parsed = value[key].asDouble();
        if (!parsed.isOk()) return false;
        result = static_cast<float>(parsed.unwrap());
        return std::isfinite(result);
    }

    bool readDouble(matjson::Value const& value, char const* key, double& result) {
        auto parsed = value[key].asDouble();
        if (!parsed.isOk()) return false;
        result = parsed.unwrap();
        return std::isfinite(result);
    }
}

MacroManager& MacroManager::get() {
    static MacroManager manager;
    return manager;
}

MacroManager::MacroManager() {
    auto* mod = Mod::get();
    if (!mod) return;
    m_autoRecordFromLiveBot = mod->getSavedValue<bool>("auto-record-live", false);
    m_blockManualPlayback = mod->getSavedValue<bool>("block-manual-playback", true);
    m_floatingButtonEnabled = mod->getSavedValue<bool>("floating-button", false);
    m_hudEnabled = mod->getSavedValue<bool>("hud-enabled", true);
    m_lookaheadMultiplier = std::clamp(
        mod->getSavedValue<float>("lookahead-multiplier", 1.f), 0.70f, 1.40f
    );
}

BotMode MacroManager::mode() const { return m_mode; }

void MacroManager::setMode(BotMode mode) {
    if (mode == BotMode::LiveBot || mode == BotMode::Playback) activateSafeModeForAttempt();
    if (m_mode == mode) {
        if (mode == BotMode::Record) resetAttempt(true);
        if (mode == BotMode::Playback) {
            releasePlaybackButtons(m_lastPlayer1, m_lastPlayer2);
            m_playbackCursor = 0;
            m_desyncWaitTicks = 0;
        }
        return;
    }
    if (m_mode == BotMode::Playback && mode != BotMode::Playback) {
        releasePlaybackButtons(m_lastPlayer1, m_lastPlayer2);
    }
    m_mode = mode;
    if (mode == BotMode::Record) {
        m_actions.clear();
        m_checkpoints.clear();
        m_checkpointId = 0;
        resetAttempt(false);
    } else if (mode == BotMode::Playback) {
        m_playbackCursor = 0;
        m_desyncWaitTicks = 0;
        std::fill(m_playbackHeld[0], m_playbackHeld[0] + 3, false);
        std::fill(m_playbackHeld[1], m_playbackHeld[1] + 3, false);
    }
}

char const* MacroManager::modeName(BotMode mode) {
    switch (mode) {
        case BotMode::Off: return "OFF";
        case BotMode::LiveBot: return "LIVE BOT";
        case BotMode::Record: return "RECORD";
        case BotMode::Playback: return "PLAYBACK";
    }
    return "OFF";
}

int MacroManager::tick() const { return m_tick; }
double MacroManager::time() const { return m_time; }
size_t MacroManager::actionCount() const { return m_actions.size(); }
std::vector<MacroAction> const& MacroManager::actions() const { return m_actions; }
std::string const& MacroManager::levelName() const { return m_levelName; }
int MacroManager::levelID() const { return m_levelID; }
PlayLayer* MacroManager::activeLayer() const { return m_activeLayer; }

void MacroManager::beginLevel(PlayLayer* layer, int levelID, std::string levelName) {
    m_safeModeAttemptActive = false;
    if (m_mode == BotMode::LiveBot || m_mode == BotMode::Playback) activateSafeModeForAttempt();
    m_activeLayer = layer;
    m_levelID = levelID;
    m_levelName = levelName.empty() ? "Unknown Level" : std::move(levelName);
    m_checkpoints.clear();
    m_checkpointId = 0;
    resetAttempt(m_mode == BotMode::Record
        || (m_mode == BotMode::LiveBot && m_autoRecordFromLiveBot));
    m_lastPlayer1 = nullptr;
    m_lastPlayer2 = nullptr;
}

void MacroManager::endLevel(PlayLayer* layer) {
    if (m_activeLayer != layer) return;
    m_safeModeAttemptActive = false;
    m_activeLayer = nullptr;
    m_lastPlayer1 = nullptr;
    m_lastPlayer2 = nullptr;
}

void MacroManager::onPhysicsStep(PlayLayer* layer, PlayerObject* player1, PlayerObject* player2, float dt) {
    if (m_activeLayer != layer) {
        m_activeLayer = layer;
        m_checkpoints.clear();
        m_checkpointId = 0;
        resetAttempt(m_mode == BotMode::Record
            || (m_mode == BotMode::LiveBot && m_autoRecordFromLiveBot));
    }
    m_lastPlayer1 = player1;
    m_lastPlayer2 = player2;
    ++m_tick;
    if (std::isfinite(dt) && dt > 0.f) m_time += static_cast<double>(dt);
    if (m_mode == BotMode::Playback) applyPlayback(player1, player2);
}

void MacroManager::captureButton(PlayerObject* player, int button, bool down) {
    if (!player || button < 1 || button > 3) return;
    int playerNumber = 0;
    auto* player1 = m_activeLayer ? m_activeLayer->m_player1 : m_lastPlayer1;
    auto* player2 = m_activeLayer ? m_activeLayer->m_player2 : m_lastPlayer2;
    if (player == player1) playerNumber = 1;
    else if (player == player2) playerNumber = 2;
    if (playerNumber == 0) return;

    auto const shouldRecord = (m_mode == BotMode::Record && m_inputOrigin == InputOrigin::Manual)
        || (m_mode == BotMode::LiveBot && m_autoRecordFromLiveBot && m_inputOrigin == InputOrigin::LiveBot);
    if (!shouldRecord) return;

    auto const position = player->getPosition();
    m_actions.push_back(MacroAction {
        .tick = m_tick,
        .time = m_time,
        .player = playerNumber,
        .button = button,
        .down = down,
        .posX = position.x,
        .posY = position.y,
        .upsideDown = player->m_isUpsideDown,
        .checkpointId = m_checkpointId,
    });
}

void MacroManager::sendButton(PlayerObject* player, int button, bool down, InputOrigin origin) {
    if (!player || button < 1 || button > 3) return;
    auto const previousOrigin = m_inputOrigin;
    m_inputOrigin = origin;
    auto const geodeButton = static_cast<PlayerButton>(button);
    if (down) player->pushButton(geodeButton);
    else player->releaseButton(geodeButton);
    m_inputOrigin = previousOrigin;
}

bool MacroManager::shouldBlockManualInput() const {
    return m_mode == BotMode::Playback && m_blockManualPlayback
        && m_inputOrigin == InputOrigin::Manual;
}

InputOrigin MacroManager::inputOrigin() const { return m_inputOrigin; }

void MacroManager::applyPlayback(PlayerObject* player1, PlayerObject* player2) {
    while (m_playbackCursor < m_actions.size()) {
        auto const& action = m_actions[m_playbackCursor];
        auto const due = action.tick > 0
            ? (m_tick >= action.tick || m_time + 1e-6 >= action.time)
            : (m_time + 1e-6 >= action.time);
        if (!due) break;

        auto* player = action.player == 2 ? player2 : player1;
        if (!player) {
            if (++m_desyncWaitTicks <= kMaxDesyncWait) break;
            ++m_playbackCursor;
            m_desyncWaitTicks = 0;
            continue;
        }

        auto const positionError = static_cast<double>(player->getPositionX()) - action.posX;
        if (action.down && positionError < -kPositionTolerance
            && ++m_desyncWaitTicks <= kMaxDesyncWait) {
            break;
        }
        if (action.down && positionError > kPositionTolerance) {
            // The attempt has moved beyond this recorded press. Skip it and
            // preserve ordering so a later release cannot create a stuck input.
            ++m_playbackCursor;
            m_desyncWaitTicks = 0;
            continue;
        }

        auto const playerIndex = action.player - 1;
        auto const buttonIndex = action.button - 1;
        if (m_playbackHeld[playerIndex][buttonIndex] != action.down) {
            sendButton(player, action.button, action.down, InputOrigin::Playback);
            m_playbackHeld[playerIndex][buttonIndex] = action.down;
        }
        ++m_playbackCursor;
        m_desyncWaitTicks = 0;
    }

    if (m_playbackCursor >= m_actions.size()) releasePlaybackButtons(player1, player2);
}

void MacroManager::releasePlaybackButtons(PlayerObject* player1, PlayerObject* player2) {
    PlayerObject* players[2] = { player1, player2 };
    for (int p = 0; p < 2; ++p) {
        for (int b = 0; b < 3; ++b) {
            if (!m_playbackHeld[p][b]) continue;
            if (players[p]) sendButton(players[p], b + 1, false, InputOrigin::Playback);
            m_playbackHeld[p][b] = false;
        }
    }
}

void MacroManager::onCheckpointPlaced() {
    ++m_checkpointId;
    m_checkpoints.push_back(CheckpointMark {
        .actionCount = m_actions.size(),
        .playbackCursor = m_playbackCursor,
        .tick = m_tick,
        .time = m_time,
        .id = m_checkpointId,
    });
}

void MacroManager::onLevelReset(bool checkpointRestore) {
    releasePlaybackButtons(m_lastPlayer1, m_lastPlayer2);
    // Checkpoint restores belong to the same attempt; a full restart resets it.
    if (!checkpointRestore) {
        m_safeModeAttemptActive = false;
        if (m_mode == BotMode::LiveBot || m_mode == BotMode::Playback) activateSafeModeForAttempt();
    }
    if (checkpointRestore && !m_checkpoints.empty()) {
        auto const mark = m_checkpoints.back();
        if (m_mode == BotMode::Record || (m_mode == BotMode::LiveBot && m_autoRecordFromLiveBot)) {
            if (m_actions.size() > mark.actionCount) m_actions.resize(mark.actionCount);
        }
        m_tick = mark.tick;
        m_time = mark.time;
        m_checkpointId = mark.id;
        m_playbackCursor = mark.playbackCursor;
        m_desyncWaitTicks = 0;
        std::fill(m_playbackHeld[0], m_playbackHeld[0] + 3, false);
        std::fill(m_playbackHeld[1], m_playbackHeld[1] + 3, false);
        return;
    }
    resetAttempt(m_mode == BotMode::Record || (m_mode == BotMode::LiveBot && m_autoRecordFromLiveBot));
    m_checkpoints.clear();
    m_checkpointId = 0;
}

void MacroManager::resetAttempt(bool clearRecordedActions) {
    if (clearRecordedActions) m_actions.clear();
    m_tick = 0;
    m_time = 0.0;
    m_playbackCursor = 0;
    m_desyncWaitTicks = 0;
    std::fill(m_playbackHeld[0], m_playbackHeld[0] + 3, false);
    std::fill(m_playbackHeld[1], m_playbackHeld[1] + 3, false);
}

bool MacroManager::autoRecordFromLiveBot() const { return m_autoRecordFromLiveBot; }
void MacroManager::setAutoRecordFromLiveBot(bool enabled) {
    m_autoRecordFromLiveBot = enabled;
    Mod::get()->setSavedValue("auto-record-live", enabled);
}
bool MacroManager::blockManualPlayback() const { return m_blockManualPlayback; }
void MacroManager::setBlockManualPlayback(bool enabled) {
    m_blockManualPlayback = enabled;
    Mod::get()->setSavedValue("block-manual-playback", enabled);
}
bool MacroManager::floatingButtonEnabled() const { return m_floatingButtonEnabled; }
void MacroManager::setFloatingButtonEnabled(bool enabled) {
    m_floatingButtonEnabled = enabled;
    Mod::get()->setSavedValue("floating-button", enabled);
}
bool MacroManager::hudEnabled() const { return m_hudEnabled; }
void MacroManager::setHudEnabled(bool enabled) {
    m_hudEnabled = enabled;
    Mod::get()->setSavedValue("hud-enabled", enabled);
}
float MacroManager::lookaheadMultiplier() const { return m_lookaheadMultiplier; }
void MacroManager::setLookaheadMultiplier(float value) {
    m_lookaheadMultiplier = std::clamp(value, 0.70f, 1.40f);
    Mod::get()->setSavedValue("lookahead-multiplier", m_lookaheadMultiplier);
}
bool MacroManager::safeMode() const { return m_safeModeAttemptActive; }
void MacroManager::activateSafeModeForAttempt() { m_safeModeAttemptActive = true; }

void MacroManager::clearMacro() {
    m_actions.clear();
    m_checkpoints.clear();
    m_checkpointId = 0;
    resetAttempt(false);
}

asp::fs::Path MacroManager::macroDirectory() const {
    return Mod::get()->getSaveDir() / "macros";
}

asp::fs::Path MacroManager::pathForName(std::string const& name) const {
    return macroDirectory() / (sanitize(name) + ".json");
}

std::string MacroManager::buildJson(std::string const& name) const {
    auto root = matjson::Value::object();
    root["name"] = name;
    root["levelID"] = m_levelID;
    root["levelName"] = m_levelName;
    root["creator"] = "FrancoGarcia";
    root["gdVersion"] = "2.2091";
    root["modVersion"] = "1.0.0";
    root["totalTicks"] = m_tick;
    root["durationSeconds"] = m_time;
    root["actionCount"] = static_cast<int>(m_actions.size());
    root["createdAt"] = timestampNow();

    auto actions = matjson::Value::array();
    for (auto const& action : m_actions) {
        auto item = matjson::Value::object();
        item["tick"] = action.tick;
        item["time"] = action.time;
        item["player"] = action.player;
        item["button"] = action.button;
        item["down"] = action.down;
        item["posX"] = action.posX;
        item["posY"] = action.posY;
        item["upsideDown"] = action.upsideDown;
        item["checkpointId"] = action.checkpointId;
        actions.push(std::move(item));
    }
    root["actions"] = std::move(actions);
    return root.dump(2);
}

bool MacroManager::saveMacro(std::string const& name, std::string& error) {
    if (m_actions.empty()) {
        error = "La macro actual no tiene acciones.";
        return false;
    }
    auto const directory = macroDirectory();
    auto const path = directory / (sanitize(name) + ".json");
    auto const createResult = utils::file::createDirectoryAll(directory);
    if (!createResult) {
        error = "No se pudo crear la carpeta de macros.";
        return false;
    }
    auto const result = utils::file::writeStringSafe(path, buildJson(name));
    if (!result) {
        error = "No se pudo guardar el archivo JSON.";
        return false;
    }
    return true;
}

bool MacroManager::loadMacro(asp::fs::Path const& path, std::string& error) {
    auto result = utils::file::readString(path);
    if (!result) {
        error = "No se pudo leer el archivo seleccionado.";
        return false;
    }
    return parseJson(result.unwrap(), error);
}

bool MacroManager::loadMacroByName(std::string const& name, std::string& error) {
    return loadMacro(pathForName(name), error);
}

bool MacroManager::importJson(std::string const& json, std::string& error) {
    return parseJson(json, error);
}

bool MacroManager::importFile(asp::fs::Path const& path, std::string& error) {
    return loadMacro(path, error);
}

bool MacroManager::parseJson(std::string const& json, std::string& error) {
    auto parsed = matjson::parse(json);
    if (!parsed.isOk()) {
        error = "El contenido no es JSON válido.";
        return false;
    }
    auto const& root = parsed.unwrap();
    if (!root.isObject()) {
        error = "La raíz del JSON debe ser un objeto.";
        return false;
    }
    constexpr char const* required[] = {
        "name", "levelID", "levelName", "creator", "gdVersion", "modVersion",
        "totalTicks", "durationSeconds", "actionCount", "createdAt", "actions"
    };
    for (auto const* key : required) {
        if (!root.contains(key)) {
            error = std::string("Falta el metadato requerido: ") + key;
            return false;
        }
    }
    if (!root["actions"].isArray()) {
        error = "El campo actions debe ser una lista.";
        return false;
    }
    auto nameResult = root["name"].asString();
    auto levelNameResult = root["levelName"].asString();
    auto creatorResult = root["creator"].asString();
    auto gdVersionResult = root["gdVersion"].asString();
    auto modVersionResult = root["modVersion"].asString();
    auto createdAtResult = root["createdAt"].asString();
    auto idResult = root["levelID"].asInt();
    auto ticksResult = root["totalTicks"].asInt();
    auto durationResult = root["durationSeconds"].asDouble();
    auto actionCountResult = root["actionCount"].asInt();
    if (!nameResult.isOk() || !levelNameResult.isOk() || !creatorResult.isOk()
        || !gdVersionResult.isOk() || !modVersionResult.isOk() || !createdAtResult.isOk()
        || !idResult.isOk() || !ticksResult.isOk() || !durationResult.isOk()
        || !actionCountResult.isOk()) {
        error = "Uno o más metadatos de la macro tienen un tipo inválido.";
        return false;
    }
    if (idResult.unwrap() < std::numeric_limits<int>::min()
        || idResult.unwrap() > std::numeric_limits<int>::max()
        || ticksResult.unwrap() < 0 || ticksResult.unwrap() > std::numeric_limits<int>::max()
        || actionCountResult.unwrap() < 0
        || static_cast<uint64_t>(actionCountResult.unwrap()) != root["actions"].size()
        || !std::isfinite(durationResult.unwrap()) || durationResult.unwrap() < 0.0) {
        error = "Los metadatos de la macro contienen valores inconsistentes o fuera de rango.";
        return false;
    }

    std::vector<MacroAction> parsedActions;
    auto const& list = root["actions"];
    constexpr size_t kMaximumImportedActions = 2'000'000;
    if (list.size() > kMaximumImportedActions) {
        error = "La macro supera el límite de acciones permitido.";
        return false;
    }
    parsedActions.reserve(list.size());
    for (size_t index = 0; index < list.size(); ++index) {
        auto const& item = list[index];
        MacroAction action;
        auto downResult = item["down"].asBool();
        auto flipResult = item["upsideDown"].asBool();
        if (!readInt(item, "tick", action.tick)
            || !readDouble(item, "time", action.time)
            || !readInt(item, "player", action.player)
            || !readInt(item, "button", action.button)
            || !readFloat(item, "posX", action.posX)
            || !readFloat(item, "posY", action.posY)
            || !readInt(item, "checkpointId", action.checkpointId)
            || !downResult.isOk() || !flipResult.isOk()) {
            error = fmt::format("La acción {} tiene campos inválidos.", index + 1);
            return false;
        }
        action.down = downResult.unwrap();
        action.upsideDown = flipResult.unwrap();
        if (action.tick < 0 || action.time < 0.0 || action.player < 1 || action.player > 2
            || action.button < 1 || action.button > 3 || action.checkpointId < 0) {
            error = fmt::format("La acción {} tiene valores fuera de rango.", index + 1);
            return false;
        }
        parsedActions.push_back(action);
    }

    std::stable_sort(parsedActions.begin(), parsedActions.end(), [](auto const& a, auto const& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return a.time < b.time;
    });
    setMode(BotMode::Off);
    m_actions = std::move(parsedActions);
    m_levelName = levelNameResult.unwrap();
    m_levelID = static_cast<int>(idResult.unwrap());
    m_playbackCursor = 0;
    m_checkpoints.clear();
    m_checkpointId = 0;
    return true;
}

bool MacroManager::exportMacro(std::string const& name, asp::fs::Path& outputPath, std::string& error) {
    if (m_actions.empty()) {
        error = "No hay acciones para exportar.";
        return false;
    }
    auto const directory = Mod::get()->getSaveDir() / "exports";
    auto const createResult = utils::file::createDirectoryAll(directory);
    if (!createResult) {
        error = "No se pudo crear la carpeta de exportación.";
        return false;
    }
    outputPath = directory / (sanitize(name) + ".json");
    auto const json = buildJson(name);
    auto const writeResult = utils::file::writeStringSafe(outputPath, json);
    if (!writeResult) {
        error = "No se pudo escribir el archivo de exportación.";
        return false;
    }
    if (!utils::clipboard::write(json)) {
        error = "Archivo exportado, pero no se pudo copiar el JSON al portapapeles.";
        return false;
    }
    return true;
}

bool MacroManager::deleteMacro(asp::fs::Path const& path, std::string& error) {
    auto root = macroDirectory().weaklyCanonical();
    auto candidate = path.weaklyCanonical();
    if (!root || !candidate) {
        error = "No se pudo acceder a la carpeta de macros.";
        return false;
    }
    auto const parent = candidate.unwrap().parent();
    auto const extension = candidate.unwrap().extension();
    if (!parent || parent->view() != root.unwrap().view().view()
        || !extension || *extension != "json") {
        error = "La ruta seleccionada no pertenece a la carpeta de macros.";
        return false;
    }
    if (std::remove(candidate.unwrap().c_str()) != 0) {
        error = "No se pudo borrar el archivo.";
        return false;
    }
    return true;
}

std::vector<MacroSummary> MacroManager::listMacros() const {
    std::vector<MacroSummary> macros;
    auto entries = utils::file::readDirectory(macroDirectory());
    if (!entries) return macros;
    for (auto const& path : entries.unwrap()) {
        auto const extension = path.extension();
        if (!path.isFile() || !extension || *extension != "json") continue;
        auto contents = utils::file::readString(path);
        if (!contents) continue;
        auto parsed = matjson::parse(contents.unwrap());
        if (!parsed || !parsed.unwrap()["actions"].isArray()) continue;
        auto const& root = parsed.unwrap();
        MacroSummary summary;
        summary.path = path;
        summary.name = root["name"].asString().unwrapOr(std::string(path.stem().value_or("macro")));
        summary.levelName = root["levelName"].asString().unwrapOr("Unknown Level");
        summary.levelID = static_cast<int>(root["levelID"].asInt().unwrapOr(0));
        summary.actionCount = root["actions"].size();
        macros.push_back(std::move(summary));
    }
    std::sort(macros.begin(), macros.end(), [](auto const& a, auto const& b) {
        return a.name < b.name;
    });
    return macros;
}

void MacroManager::saveOnLevelComplete() {
    if (m_mode != BotMode::LiveBot || !m_autoRecordFromLiveBot || m_actions.empty()) return;
    std::string error;
    auto const name = "LiveBot_" + sanitize(m_levelName) + "_" + std::to_string(m_levelID);
    if (!saveMacro(name, error)) log::warn("Live Bot macro auto-save failed: {}", error);
}
