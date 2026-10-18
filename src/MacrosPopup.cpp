#include "MacrosPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/TextInput.hpp>
#include <matjson.hpp>

#include <algorithm>
#include <functional>
#include <set>
#include <utility>

#include <Geode/binding/PlayLayer.hpp>

#include "LevelMacros.hpp"
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
    if (!Popup::init(280.f, 300.f, "GJ_square01.png")) {
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

    auto importSpr = ButtonSprite::create("Import Replay", "goldFont.fnt", "GJ_button_02.png", .8f);
    importSpr->setScale(.65f);
    auto importBtn = CCMenuItemSpriteExtra::create(
        importSpr, this, menu_selector(MacrosPopup::onImportReplay)
    );
    m_buttonMenu->addChildAtPosition(importBtn, Anchor::Center, ccp(-62, -126));

    auto folderSpr = ButtonSprite::create("Imports Folder", "goldFont.fnt", "GJ_button_02.png", .8f);
    folderSpr->setScale(.65f);
    auto folderBtn = CCMenuItemSpriteExtra::create(
        folderSpr, this, menu_selector(MacrosPopup::onOpenImportsFolder)
    );
    m_buttonMenu->addChildAtPosition(folderBtn, Anchor::Center, ccp(62, -126));

    // Replays dropped into the imports folder are picked up on open.
    importPending();
    reloadMacros();

    return true;
}

void MacrosPopup::importReplayFile(std::string const& path) {
    // blockwork_import_gdr doesn't report the new macro's id, so find it by
    // diffing the library before and after.
    std::set<std::string> before;
    for (auto& m : m_macros) {
        before.insert(m.id);
    }
    int result = blockwork_import_gdr(path.c_str());
    if (result == 0) {
        reloadMacros();
        // Imported while inside a level: that replay belongs to this level.
        auto* pl = PlayLayer::get();
        if (pl) {
            for (auto& m : m_macros) {
                if (!before.count(m.id)) {
                    levelmacros::link(levelmacros::keyFor(pl->m_level), m.id);
                    break;
                }
            }
        }
        Notification::create(
            pl ? "Replay imported and linked to this level" : "Replay imported",
            NotificationIcon::Success
        )->show();
        return;
    }
    const char* why = result == -2 ? "Couldn't read that file"
        : result == -3 ? "Not a valid GDR replay"
        : "Couldn't import that replay";
    Notification::create(why, NotificationIcon::Error)->show();
}

void MacrosPopup::importPending() {
    int count = blockwork_import_pending_gdrs();
    if (count > 0) {
        Notification::create(fmt::format("Imported {} replay(s)", count), NotificationIcon::Success)->show();
    }
}

void MacrosPopup::onImportReplay(CCObject*) {
    file::FilePickOptions options;
    options.filters.push_back({"GD replays", {"*.gdr", "*.gdr2", "*.json"}});
    // Under Proton this is Wine's own file dialog; paths come back as Z:\...
    // which blockwork-ffi (also inside Wine) can read directly.
    m_pickHolder.spawn("Import replay", file::pick(file::PickMode::OpenFile, options), [this](file::PickResult res) {
        if (!res) {
            Notification::create("Couldn't open the file picker - drop the replay into the Imports Folder instead", NotificationIcon::Warning)->show();
            return;
        }
        if (auto path = res.unwrap()) {
            importReplayFile(path->string());
        }
    });
}

void MacrosPopup::onOpenImportsFolder(CCObject*) {
    char* dir = blockwork_imports_dir();
    if (!dir) {
        Notification::create("Imports folder unavailable", NotificationIcon::Error)->show();
        return;
    }
    std::string path = dir;
    blockwork_free_string(dir);
#ifdef GEODE_IS_WINDOWS
    // Under Wine the folder lives on the Linux side (Z:\home\...): hand it to
    // the host's file manager with `start /unix`, since Explorer can't show it.
    if (GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version")) {
        std::string unixPath = path;
        if (unixPath.size() > 1 && unixPath[1] == ':') {
            unixPath = unixPath.substr(2);
        }
        std::replace(unixPath.begin(), unixPath.end(), '\\', '/');
        std::string cmd = "cmd.exe /c start /unix \"" + unixPath + "\"";
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return;
        }
    }
#endif
    file::openFolder(std::filesystem::path(path));
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
    // Picking a macro while in a level is how the player (re)assigns it.
    if (auto* pl = PlayLayer::get()) {
        levelmacros::link(levelmacros::keyFor(pl->m_level), m_macros[m_selectedIndex].id);
    }
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
    // create_macro selects the new macro; tie it to the current level, if any.
    if (auto* pl = PlayLayer::get()) {
        levelmacros::linkSelectedTo(pl->m_level);
    }
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
