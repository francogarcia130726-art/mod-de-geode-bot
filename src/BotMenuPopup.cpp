#include "BotMenuPopup.hpp"

#include "LiveBot.hpp"

#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>

using namespace geode::prelude;

namespace {
    void addText(CCNode* parent, std::string const& text, CCPoint position, float scale = 0.42f,
        char const* font = "bigFont.fnt") {
        auto* label = CCLabelBMFont::create(text.c_str(), font);
        if (!label) return;
        label->setScale(scale);
        label->setPosition(position);
        parent->addChild(label);
    }

    void addLeftText(CCNode* parent, std::string const& text, CCPoint position, float scale = 0.30f) {
        auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        if (!label) return;
        label->setScale(scale);
        label->setAnchorPoint({ 0.f, 0.5f });
        label->setPosition(position);
        parent->addChild(label);
    }

    void addCenteredText(CCNode* parent, std::string const& text, CCPoint offset, float scale = 0.42f) {
        auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        if (!label) return;
        label->setScale(scale);
        parent->addChildAtPosition(label, Anchor::Center, offset);
    }

    char const* toggleName(bool enabled) {
        return enabled ? "ON" : "OFF";
    }
}

BotMenuPopup* BotMenuPopup::create() {
    auto* popup = new BotMenuPopup();
    if (popup && popup->init()) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

void BotMenuPopup::open() {
    if (auto* popup = BotMenuPopup::create()) popup->show();
}

bool BotMenuPopup::init() {
    if (!Popup::init(450.f, 290.f)) return false;
    this->setTitle("Live Level Bot & Macro Studio", "goldFont.fnt", 0.62f, 20.f);

    m_tabMenu = CCMenu::create();
    m_tabMenu->setPosition({ 0.f, 0.f });
    m_mainLayer->addChildAtPosition(m_tabMenu, Anchor::Center, { 0.f, 102.f });
    addButton(m_tabMenu, "Bot & Control", { -130.f, 0.f }, menu_selector(BotMenuPopup::onTabControl), 0.42f);
    addButton(m_tabMenu, "Macros", { 0.f, 0.f }, menu_selector(BotMenuPopup::onTabMacros), 0.48f);
    addButton(m_tabMenu, "Ajustes", { 130.f, 0.f }, menu_selector(BotMenuPopup::onTabSettings), 0.48f);

    refreshPage();
    return true;
}

void BotMenuPopup::addButton(CCMenu* menu, char const* title, CCPoint position,
    SEL_MenuHandler callback, float scale) {
    auto* sprite = ButtonSprite::create(title, "goldFont.fnt", "GJ_button_01.png", 0.65f);
    if (!sprite) return;
    sprite->setScale(scale);
    auto* item = CCMenuItemSpriteExtra::create(sprite, this, callback);
    if (!item) return;
    if (menu == m_pageMenu && m_pageRoot) {
        auto const size = m_pageRoot->getContentSize();
        item->setPosition({ size.width * 0.5f + position.x, size.height * 0.5f + position.y });
    } else {
        item->setPosition(position);
    }
    menu->addChild(item);
}

void BotMenuPopup::showMessage(char const* title, std::string const& message) {
    FLAlertLayer::create(title, message, "OK")->show();
}

std::string BotMenuPopup::currentMacroName() const {
    if (m_nameInput) {
        auto const value = m_nameInput->getString();
        if (value.c_str() && *value.c_str()) return value.c_str();
    }
    if (!m_macroName.empty()) return m_macroName;
    return MacroManager::get().levelName();
}

void BotMenuPopup::switchTab(int tab) {
    m_tab = std::clamp(tab, 0, 2);
    refreshPage();
}

void BotMenuPopup::refreshPage() {
    m_nameInput = nullptr;
    if (m_pageRoot) {
        m_pageRoot->removeFromParentAndCleanup(true);
        m_pageRoot = nullptr;
        m_pageMenu = nullptr;
    }
    m_pageRoot = CCNode::create();
    m_pageRoot->setContentSize({ 430.f, 190.f });
    m_mainLayer->addChildAtPosition(m_pageRoot, Anchor::Center, { 0.f, -5.f });
    m_pageMenu = CCMenu::create();
    m_pageMenu->setContentSize(m_pageRoot->getContentSize());
    m_pageMenu->setPosition({ 215.f, 95.f });
    m_pageRoot->addChild(m_pageMenu);

    switch (m_tab) {
        case 0: renderControlPage(); break;
        case 1: renderMacroPage(); break;
        case 2: renderSettingsPage(); break;
    }
}

void BotMenuPopup::renderControlPage() {
    auto& manager = MacroManager::get();
    addCenteredText(m_pageRoot, fmt::format("Estado: {}   |   Nivel: {}", MacroManager::modeName(manager.mode()), manager.levelName()), { 0.f, 70.f });
    addCenteredText(m_pageRoot, fmt::format("Acciones: {}   |   Tick: {}   |   {:.2f}s", manager.actionCount(), manager.tick(), manager.time()), { 0.f, 48.f }, 0.36f);

    addButton(m_pageMenu, "OFF", { -150.f, 15.f }, menu_selector(BotMenuPopup::onModeOff), 0.42f);
    addButton(m_pageMenu, "LIVE BOT", { -50.f, 15.f }, menu_selector(BotMenuPopup::onModeLive), 0.42f);
    addButton(m_pageMenu, "RECORD", { 50.f, 15.f }, menu_selector(BotMenuPopup::onModeRecord), 0.42f);
    addButton(m_pageMenu, "PLAYBACK", { 150.f, 15.f }, menu_selector(BotMenuPopup::onModePlayback), 0.42f);

    addButton(m_pageMenu,
        fmt::format("Auto-Grabar Live Bot: {}", toggleName(manager.autoRecordFromLiveBot())).c_str(),
        { -75.f, -35.f }, menu_selector(BotMenuPopup::onToggleAutoRecord), 0.47f);
    addButton(m_pageMenu, "Limpiar Macro Actual", { 95.f, -35.f }, menu_selector(BotMenuPopup::onClearMacro), 0.47f);
}

void BotMenuPopup::renderMacroPage() {
    auto& manager = MacroManager::get();
    m_nameInput = TextInput::create(220.f, "Nombre de la macro");
    if (m_nameInput) {
        m_nameInput->setMaxCharCount(64);
        m_nameInput->setCommonFilter(CommonFilter::Name);
        if (!m_macroName.empty()) m_nameInput->setString(m_macroName.c_str());
        m_nameInput->setCallback([this](std::string const& value) { m_macroName = value; });
        m_pageRoot->addChildAtPosition(m_nameInput, Anchor::Center, { -90.f, 66.f });
    }
    addButton(m_pageMenu, "Usar nombre del nivel", { 130.f, 66.f }, menu_selector(BotMenuPopup::onUseLevelName), 0.39f);

    addButton(m_pageMenu, "Guardar", { -145.f, 22.f }, menu_selector(BotMenuPopup::onSaveMacro), 0.45f);
    addButton(m_pageMenu, "Exportar JSON", { -50.f, 22.f }, menu_selector(BotMenuPopup::onExportMacro), 0.43f);
    addButton(m_pageMenu, "Importar Archivo", { 50.f, 22.f }, menu_selector(BotMenuPopup::onImportFile), 0.43f);
    addButton(m_pageMenu, "Pegar Portapapeles", { 150.f, 22.f }, menu_selector(BotMenuPopup::onImportClipboard), 0.42f);

    m_macros = manager.listMacros();
    auto* scroll = ScrollLayer::create(CCSize { 400.f, 92.f }, false, true);
    if (!scroll) return;
    scroll->ignoreAnchorPointForPosition(false);
    m_pageRoot->addChildAtPosition(scroll, Anchor::Center, { 0.f, -72.f });
    auto const contentHeight = std::max(92.f, static_cast<float>(m_macros.size()) * 32.f);
    scroll->m_contentLayer->setContentSize({ 400.f, contentHeight });

    if (m_macros.empty()) {
        addText(scroll->m_contentLayer, "No hay macros guardadas todavía.", { 200.f, 48.f }, 0.36f);
        return;
    }

    for (size_t i = 0; i < m_macros.size(); ++i) {
        auto const& macro = m_macros[i];
        auto const rowY = contentHeight - 18.f - static_cast<float>(i) * 32.f;
        addLeftText(scroll->m_contentLayer,
            fmt::format("{}  |  {}  |  {} acciones", macro.name, macro.levelName, macro.actionCount),
            { 12.f, rowY }, 0.28f);

        auto* rowMenu = CCMenu::create();
        rowMenu->setContentSize({ 400.f, contentHeight });
        rowMenu->setPosition({ 200.f, contentHeight * 0.5f });
        auto* loadSprite = ButtonSprite::create("Cargar", "goldFont.fnt", "GJ_button_05.png", 0.5f);
        auto* deleteSprite = ButtonSprite::create("Borrar", "goldFont.fnt", "GJ_button_06.png", 0.5f);
        if (loadSprite && deleteSprite) {
            loadSprite->setScale(0.35f);
            deleteSprite->setScale(0.35f);
            auto* load = CCMenuItemSpriteExtra::create(loadSprite, this, menu_selector(BotMenuPopup::onLoadMacro));
            auto* remove = CCMenuItemSpriteExtra::create(deleteSprite, this, menu_selector(BotMenuPopup::onDeleteMacro));
            load->setPosition({ 318.f, rowY });
            remove->setPosition({ 372.f, rowY });
            load->setTag(static_cast<int>(i));
            remove->setTag(static_cast<int>(i));
            rowMenu->addChild(load);
            rowMenu->addChild(remove);
            scroll->m_contentLayer->addChild(rowMenu);
        }
    }
}

void BotMenuPopup::renderSettingsPage() {
    auto& manager = MacroManager::get();
    addCenteredText(m_pageRoot, "Control, visualización y anticipación", { 0.f, 70.f }, 0.40f);

    addButton(m_pageMenu,
        fmt::format("Botón flotante en juego: {}", toggleName(manager.floatingButtonEnabled())).c_str(),
        { 0.f, 38.f }, menu_selector(BotMenuPopup::onToggleFloating), 0.45f);
    addButton(m_pageMenu,
        fmt::format("HUD de estado: {}", toggleName(manager.hudEnabled())).c_str(),
        { 0.f, 2.f }, menu_selector(BotMenuPopup::onToggleHud), 0.45f);
    addButton(m_pageMenu,
        fmt::format("Bloquear toque en Playback: {}", toggleName(manager.blockManualPlayback())).c_str(),
        { 0.f, -34.f }, menu_selector(BotMenuPopup::onTogglePlaybackLock), 0.45f);
    addButton(m_pageMenu,
        fmt::format("Anticipación Live Bot: {:.2f}x", manager.lookaheadMultiplier()).c_str(),
        { 0.f, -72.f }, menu_selector(BotMenuPopup::onCycleLookahead), 0.45f);
}

void BotMenuPopup::onTabControl(CCObject*) { switchTab(0); }
void BotMenuPopup::onTabMacros(CCObject*) { switchTab(1); }
void BotMenuPopup::onTabSettings(CCObject*) { switchTab(2); }

void BotMenuPopup::onModeOff(CCObject*) {
    if (auto* layer = MacroManager::get().activeLayer()) LiveBot::get().releaseAll(layer);
    MacroManager::get().setMode(BotMode::Off);
    refreshPage();
}

void BotMenuPopup::onModeLive(CCObject*) {
    if (auto* layer = MacroManager::get().activeLayer()) LiveBot::get().releaseAll(layer);
    MacroManager::get().setMode(BotMode::LiveBot);
    if (auto* layer = MacroManager::get().activeLayer()) LiveBot::get().reset(layer);
    refreshPage();
}

void BotMenuPopup::onModeRecord(CCObject*) {
    if (auto* layer = MacroManager::get().activeLayer()) LiveBot::get().releaseAll(layer);
    auto& manager = MacroManager::get();
    manager.setMode(manager.mode() == BotMode::Record ? BotMode::Off : BotMode::Record);
    refreshPage();
}

void BotMenuPopup::onModePlayback(CCObject*) {
    auto& manager = MacroManager::get();
    if (manager.actionCount() == 0) {
        showMessage("Macro Playback", "Carga o graba una macro antes de iniciar Playback.");
        return;
    }
    if (auto* layer = manager.activeLayer()) LiveBot::get().releaseAll(layer);
    manager.setMode(BotMode::Playback);
    refreshPage();
}

void BotMenuPopup::onToggleAutoRecord(CCObject*) {
    auto& manager = MacroManager::get();
    manager.setAutoRecordFromLiveBot(!manager.autoRecordFromLiveBot());
    refreshPage();
}

void BotMenuPopup::onClearMacro(CCObject*) {
    MacroManager::get().clearMacro();
    refreshPage();
}

void BotMenuPopup::onUseLevelName(CCObject*) {
    m_macroName = MacroManager::get().levelName();
    if (m_nameInput) m_nameInput->setString(m_macroName.c_str());
}

void BotMenuPopup::onSaveMacro(CCObject*) {
    std::string error;
    auto const name = currentMacroName();
    if (!MacroManager::get().saveMacro(name, error)) {
        showMessage("No se pudo guardar", error);
        return;
    }
    showMessage("Macro guardada", fmt::format("Guardada como {}.json en la carpeta de datos del mod.", name));
    refreshPage();
}

void BotMenuPopup::onExportMacro(CCObject*) {
    std::string error;
    std::filesystem::path path;
    auto const name = currentMacroName();
    if (!MacroManager::get().exportMacro(name, path, error)) {
        showMessage("Exportación", error);
        return;
    }
    showMessage("Macro exportada", fmt::format("Archivo: {}\nJSON copiado al portapapeles.", path.string()));
}

void BotMenuPopup::onImportFile(CCObject*) {
    async::spawn(file::pick(file::PickMode::OpenFile, file::FilePickOptions {
        .filters = { file::FilePickOptions::Filter {
            .description = "Macros JSON",
            .files = { "*.json" },
        }}
    }), [](Result<std::optional<std::filesystem::path>> result) {
        if (!result) {
            FLAlertLayer::create("Importar macro", "No se pudo abrir el selector de archivos.", "OK")->show();
            return;
        }
        auto selected = std::move(result).unwrap();
        if (!selected) return;
        std::string error;
        if (auto* layer = MacroManager::get().activeLayer()) LiveBot::get().releaseAll(layer);
        if (!MacroManager::get().importFile(*selected, error)) {
            FLAlertLayer::create("Importación fallida", error, "OK")->show();
            return;
        }
        FLAlertLayer::create("Macro importada", "La macro se cargó en memoria. Elige Playback para reproducirla.", "OK")->show();
    });
}

void BotMenuPopup::onImportClipboard(CCObject*) {
    auto const text = utils::clipboard::read();
    std::string error;
    if (auto* layer = MacroManager::get().activeLayer()) LiveBot::get().releaseAll(layer);
    if (!MacroManager::get().importJson(text, error)) {
        showMessage("Importación fallida", error);
        return;
    }
    showMessage("Macro importada", "La macro del portapapeles se cargó en memoria. Elige Playback para reproducirla.");
    switchTab(0);
}

void BotMenuPopup::onLoadMacro(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItem*>(sender);
    if (!item || item->getTag() < 0 || static_cast<size_t>(item->getTag()) >= m_macros.size()) return;
    std::string error;
    if (auto* layer = MacroManager::get().activeLayer()) LiveBot::get().releaseAll(layer);
    if (!MacroManager::get().loadMacro(m_macros[static_cast<size_t>(item->getTag())].path, error)) {
        showMessage("No se pudo cargar", error);
        return;
    }
    MacroManager::get().setMode(BotMode::Playback);
    switchTab(0);
}

void BotMenuPopup::onDeleteMacro(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItem*>(sender);
    if (!item || item->getTag() < 0 || static_cast<size_t>(item->getTag()) >= m_macros.size()) return;
    std::string error;
    if (!MacroManager::get().deleteMacro(m_macros[static_cast<size_t>(item->getTag())].path, error)) {
        showMessage("No se pudo borrar", error);
        return;
    }
    refreshPage();
}

void BotMenuPopup::onToggleFloating(CCObject*) {
    auto& manager = MacroManager::get();
    manager.setFloatingButtonEnabled(!manager.floatingButtonEnabled());
    refreshPage();
}

void BotMenuPopup::onToggleHud(CCObject*) {
    auto& manager = MacroManager::get();
    manager.setHudEnabled(!manager.hudEnabled());
    refreshPage();
}

void BotMenuPopup::onTogglePlaybackLock(CCObject*) {
    auto& manager = MacroManager::get();
    manager.setBlockManualPlayback(!manager.blockManualPlayback());
    refreshPage();
}

void BotMenuPopup::onCycleLookahead(CCObject*) {
    constexpr float values[] = { 0.70f, 0.85f, 1.0f, 1.15f, 1.30f, 1.40f };
    auto& manager = MacroManager::get();
    auto const current = manager.lookaheadMultiplier();
    auto next = values[0];
    for (size_t i = 0; i < std::size(values); ++i) {
        if (std::fabs(values[i] - current) < 0.01f) {
            next = values[(i + 1) % std::size(values)];
            break;
        }
    }
    manager.setLookaheadMultiplier(next);
    refreshPage();
}
