#include "MacrosPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/TextInput.hpp>
#include <matjson.hpp>

#include <functional>
#include <utility>

#include "blockwork_ffi.h"

using namespace geode::prelude;

namespace {
// Owns a blockwork-ffi string for its scope and always frees it, even on an
// early return - every blockwork_ffi.h function that hands back a `char*`
// requires the caller to free it via blockwork_free_string, and this is the one
// place that happens so no call site below has to remember it.
struct FfiString {
    char* ptr;
    FfiString(char* p) : ptr(p) {}
    ~FfiString() {
        if (ptr) {
            blockwork_free_string(ptr);
        }
    }
    FfiString(FfiString const&) = delete;
    FfiString& operator=(FfiString const&) = delete;
    operator bool() const { return ptr != nullptr; }
};

// Tiny modal for editing the currently-selected macro's name, opened from
// MacrosPopup's "Rename" button. Kept file-local (not its own header) since
// nothing outside this file needs it.
class RenameMacroPopup : public geode::Popup {
protected:
    TextInput* m_input = nullptr;
    std::function<void()> m_onRenamed;

    bool init(std::string const& currentName, std::function<void()> onRenamed) {
        if (!Popup::init(240.f, 130.f, "GJ_square01.png")) {
            return false;
        }
        m_onRenamed = std::move(onRenamed);

        this->setTitle("Rename Macro");

        m_input = TextInput::create(200.f, "Macro Name");
        m_input->setString(currentName);
        m_mainLayer->addChildAtPosition(m_input, Anchor::Center, ccp(0, 10));

        auto saveSpr = ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_01.png", .8f);
        saveSpr->setScale(.8f);
        auto saveBtn = CCMenuItemSpriteExtra::create(
            saveSpr, this, menu_selector(RenameMacroPopup::onSave)
        );
        m_buttonMenu->addChildAtPosition(saveBtn, Anchor::Center, ccp(0, -35));

        return true;
    }

    void onSave(CCObject*) {
        auto name = m_input->getString();
        int result = blockwork_rename_macro(name.c_str());
        if (result != 0) {
            Notification::create("Failed to rename macro", NotificationIcon::Error)->show();
            return;
        }
        if (m_onRenamed) {
            m_onRenamed();
        }
        this->onClose(nullptr);
    }

public:
    static RenameMacroPopup* create(std::string const& currentName, std::function<void()> onRenamed) {
        auto ret = new RenameMacroPopup();
        if (ret->init(currentName, std::move(onRenamed))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};
}

bool MacrosPopup::init() {
    if (!Popup::init(280.f, 260.f, "GJ_square01.png")) {
        return false;
    }

    this->setTitle("Macros");

    auto leftArrowSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    leftArrowSpr->setScale(.6f);
    auto leftArrowBtn = CCMenuItemSpriteExtra::create(
        leftArrowSpr, this, menu_selector(MacrosPopup::onPrevMacro)
    );
    m_buttonMenu->addChildAtPosition(leftArrowBtn, Anchor::Center, ccp(-95, 82));

    auto rightArrowSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    rightArrowSpr->setFlipX(true);
    rightArrowSpr->setScale(.6f);
    auto rightArrowBtn = CCMenuItemSpriteExtra::create(
        rightArrowSpr, this, menu_selector(MacrosPopup::onNextMacro)
    );
    m_buttonMenu->addChildAtPosition(rightArrowBtn, Anchor::Center, ccp(95, 82));

    m_nameLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_mainLayer->addChildAtPosition(m_nameLabel, Anchor::Center, ccp(0, 82));

    auto renameSpr = ButtonSprite::create("Rename", "goldFont.fnt", "GJ_button_01.png", .8f);
    renameSpr->setScale(.8f);
    auto renameBtn = CCMenuItemSpriteExtra::create(
        renameSpr, this, menu_selector(MacrosPopup::onRenameMacro)
    );
    m_buttonMenu->addChildAtPosition(renameBtn, Anchor::Center, ccp(0, 40));

    auto createSpr = ButtonSprite::create("Create Macro", "goldFont.fnt", "GJ_button_01.png", .8f);
    createSpr->setScale(.8f);
    auto createBtn = CCMenuItemSpriteExtra::create(
        createSpr, this, menu_selector(MacrosPopup::onCreateMacro)
    );
    m_buttonMenu->addChildAtPosition(createBtn, Anchor::Center, ccp(0, -2));

    auto clearSpr = ButtonSprite::create("Clear Instructions", "goldFont.fnt", "GJ_button_06.png", .8f);
    clearSpr->setScale(.8f);
    auto clearBtn = CCMenuItemSpriteExtra::create(
        clearSpr, this, menu_selector(MacrosPopup::onClearInstructions)
    );
    m_buttonMenu->addChildAtPosition(clearBtn, Anchor::Center, ccp(0, -44));

    auto deleteSpr = ButtonSprite::create("Delete Macro", "goldFont.fnt", "GJ_button_06.png", .8f);
    deleteSpr->setScale(.8f);
    auto deleteBtn = CCMenuItemSpriteExtra::create(
        deleteSpr, this, menu_selector(MacrosPopup::onDeleteMacro)
    );
    m_buttonMenu->addChildAtPosition(deleteBtn, Anchor::Center, ccp(0, -86));

    reloadMacros();

    return true;
}

void MacrosPopup::reloadMacros() {
    m_macros.clear();
    m_selectedIndex = -1;

    if (FfiString listJson = blockwork_list_macros()) {
        auto parsed = matjson::parse(listJson.ptr);
        if (parsed) {
            for (auto& entry : parsed.unwrap()) {
                m_macros.push_back({
                    entry["id"].asString().unwrapOr(std::string("")),
                    entry["name"].asString().unwrapOr(std::string("")),
                });
            }
        }
    }

    if (FfiString selectedId = blockwork_get_selected_macro_id()) {
        std::string id(selectedId.ptr);
        for (size_t i = 0; i < m_macros.size(); i++) {
            if (m_macros[i].id == id) {
                m_selectedIndex = static_cast<int>(i);
                break;
            }
        }
    }

    updateSelectedLabel();
}

void MacrosPopup::updateSelectedLabel() {
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_macros.size())) {
        m_nameLabel->setString(m_macros[m_selectedIndex].name.c_str());
    } else if (m_macros.empty()) {
        m_nameLabel->setString("No macros yet");
    } else {
        m_nameLabel->setString("(none selected)");
    }
    limitNodeWidth(m_nameLabel, 170.f, .6f, .2f);
}

void MacrosPopup::selectCurrent() {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_macros.size())) {
        return;
    }
    blockwork_select_macro(m_macros[m_selectedIndex].id.c_str());
    updateSelectedLabel();
}

void MacrosPopup::onPrevMacro(CCObject*) {
    if (m_macros.empty()) {
        return;
    }
    m_selectedIndex = (m_selectedIndex < 0)
        ? 0
        : (m_selectedIndex - 1 + static_cast<int>(m_macros.size())) % static_cast<int>(m_macros.size());
    selectCurrent();
}

void MacrosPopup::onNextMacro(CCObject*) {
    if (m_macros.empty()) {
        return;
    }
    m_selectedIndex = (m_selectedIndex < 0)
        ? 0
        : (m_selectedIndex + 1) % static_cast<int>(m_macros.size());
    selectCurrent();
}

void MacrosPopup::onCreateMacro(CCObject*) {
    int result = blockwork_create_macro(nullptr);
    if (result != 0) {
        Notification::create("Failed to create macro", NotificationIcon::Error)->show();
        return;
    }
    reloadMacros();
    Notification::create("Macro created", NotificationIcon::Success)->show();
}

void MacrosPopup::onRenameMacro(CCObject*) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_macros.size())) {
        Notification::create("No macro selected", NotificationIcon::Warning)->show();
        return;
    }

    auto currentName = m_macros[m_selectedIndex].name;
    if (auto popup = RenameMacroPopup::create(currentName, [this] { reloadMacros(); })) {
        popup->show();
    }
}

void MacrosPopup::onDeleteMacro(CCObject*) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_macros.size())) {
        Notification::create("No macro selected", NotificationIcon::Warning)->show();
        return;
    }

    auto name = m_macros[m_selectedIndex].name;
    createQuickPopup(
        "Delete Macro",
        fmt::format("Permanently delete <cy>{}</c>? This cannot be undone.", name),
        "Cancel", "Delete",
        [this](FLAlertLayer*, bool btn2) {
            if (!btn2) {
                return;
            }
            int result = blockwork_delete_macro();
            if (result != 0) {
                Notification::create("Failed to delete macro", NotificationIcon::Error)->show();
                return;
            }
            reloadMacros();
            Notification::create("Macro deleted", NotificationIcon::Success)->show();
        }
    );
}

void MacrosPopup::onClearInstructions(CCObject*) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_macros.size())) {
        Notification::create("No macro selected", NotificationIcon::Warning)->show();
        return;
    }

    createQuickPopup(
        "Clear Instructions",
        "Clear every recorded instruction on this macro's <cy>recording-target strand only</c>? "
        "Other strands are untouched. This cannot be undone.",
        "Cancel", "Clear",
        [](FLAlertLayer*, bool btn2) {
            if (!btn2) {
                return;
            }
            int result = blockwork_clear_recording_target_instructions();
            if (result < 0) {
                Notification::create("Failed to clear instructions", NotificationIcon::Error)->show();
            } else {
                Notification::create(
                    fmt::format("Cleared {} instruction(s)", result), NotificationIcon::Success
                )->show();
            }
        }
    );
}

MacrosPopup* MacrosPopup::create() {
    auto ret = new MacrosPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}
