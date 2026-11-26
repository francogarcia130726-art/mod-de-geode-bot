#pragma once

#include "MacroManager.hpp"

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

class BotMenuPopup : public geode::Popup {
public:
    static BotMenuPopup* create();
    static void open();

protected:
    bool init() override;

private:
    void switchTab(int tab);
    void refreshPage();
    void renderControlPage();
    void renderMacroPage();
    void renderSettingsPage();
    void addButton(cocos2d::CCMenu* menu, char const* title, cocos2d::CCPoint position,
        cocos2d::SEL_MenuHandler callback, float scale = 0.55f);
    void showMessage(char const* title, std::string const& message);
    std::string currentMacroName() const;

    void onTabControl(cocos2d::CCObject*);
    void onTabMacros(cocos2d::CCObject*);
    void onTabSettings(cocos2d::CCObject*);
    void onModeOff(cocos2d::CCObject*);
    void onModeLive(cocos2d::CCObject*);
    void onModeRecord(cocos2d::CCObject*);
    void onModePlayback(cocos2d::CCObject*);
    void onToggleAutoRecord(cocos2d::CCObject*);
    void onClearMacro(cocos2d::CCObject*);
    void onUseLevelName(cocos2d::CCObject*);
    void onSaveMacro(cocos2d::CCObject*);
    void onExportMacro(cocos2d::CCObject*);
    void onImportFile(cocos2d::CCObject*);
    void onImportClipboard(cocos2d::CCObject*);
    void onLoadMacro(cocos2d::CCObject*);
    void onDeleteMacro(cocos2d::CCObject*);
    void onToggleFloating(cocos2d::CCObject*);
    void onToggleHud(cocos2d::CCObject*);
    void onTogglePlaybackLock(cocos2d::CCObject*);
    void onCycleLookahead(cocos2d::CCObject*);

    int m_tab = 0;
    cocos2d::CCMenu* m_tabMenu = nullptr;
    cocos2d::CCMenu* m_pageMenu = nullptr;
    cocos2d::CCNode* m_pageRoot = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    std::string m_macroName;
    std::vector<MacroSummary> m_macros;
};
