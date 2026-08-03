#pragma once

#include <Geode/ui/Popup.hpp>

#include <string>
#include <vector>

// Small in-game panel for the parts of macro management that don't need the
// full desktop editor: which macro is "current" (the one recording/running
// targets), spinning up a fresh one, and wiping a stale recording without
// leaving the level. Opened via the "open-macros" keybind, same pattern as
// the settings popup's "open-config" keybind in main.cpp.
class MacrosPopup : public geode::Popup {
protected:
    struct MacroEntry {
        std::string id;
        std::string name;
    };

    std::vector<MacroEntry> m_macros;
    // Index into m_macros, or -1 if nothing is selected yet (fresh install,
    // no macros exist, or the previously-selected macro's file is gone).
    int m_selectedIndex = -1;

    cocos2d::CCLabelBMFont* m_nameLabel = nullptr;

    bool init();

    // Re-reads the macro list and current selection from macros-ffi (macro
    // files are also written to by the desktop app and by recording, so this
    // is called every time the popup opens rather than cached).
    void reloadMacros();
    void updateSelectedLabel();
    // Tells macros-ffi about m_selectedIndex and refreshes the label; a
    // no-op if the index isn't currently valid.
    void selectCurrent();

    void onPrevMacro(cocos2d::CCObject* sender);
    void onNextMacro(cocos2d::CCObject* sender);
    void onCreateMacro(cocos2d::CCObject* sender);
    void onRenameMacro(cocos2d::CCObject* sender);
    void onDeleteMacro(cocos2d::CCObject* sender);
    void onClearInstructions(cocos2d::CCObject* sender);

public:
    static MacrosPopup* create();
};
