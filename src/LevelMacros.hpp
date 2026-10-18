#pragma once

#include <Geode/Geode.hpp>

#include <map>
#include <optional>
#include <string>

#include "blockwork_ffi.h"

// Remembers which macro belongs to which level, so launching a level runs its
// own macro without the player picking one first. Stored in this mod's saved
// values (macro ids come from blockwork-core, level keys from the game).
namespace levelmacros {

constexpr auto kSaveKey = "level-macros";

// Online levels are keyed by id; local (editor) levels have no id, so by name.
inline std::string keyFor(GJGameLevel* level) {
    if (!level) {
        return "";
    }
    int id = level->m_levelID.value();
    if (id > 0) {
        return fmt::format("id:{}", id);
    }
    return fmt::format("local:{}", std::string(level->m_levelName));
}

inline std::map<std::string, std::string> load() {
    return geode::Mod::get()->getSavedValue<std::map<std::string, std::string>>(kSaveKey);
}

inline void link(std::string const& levelKey, std::string const& macroId) {
    if (levelKey.empty() || macroId.empty()) {
        return;
    }
    auto map = load();
    map[levelKey] = macroId;
    geode::Mod::get()->setSavedValue(kSaveKey, map);
}

inline std::optional<std::string> linkedMacro(std::string const& levelKey) {
    auto map = load();
    auto it = map.find(levelKey);
    if (it == map.end()) {
        return std::nullopt;
    }
    return it->second;
}

// Id of the macro blockwork currently targets, or "" if none.
inline std::string selectedMacroId() {
    char* raw = blockwork_get_selected_macro_id();
    if (!raw) {
        return "";
    }
    std::string id = raw;
    blockwork_free_string(raw);
    return id;
}

// Links whatever macro is currently selected to `level`.
inline void linkSelectedTo(GJGameLevel* level) {
    link(keyFor(level), selectedMacroId());
}

// Selects `level`'s linked macro (if any, and if it still exists) so the
// upcoming run targets it. Returns true if a linked macro was selected.
inline bool selectLinkedFor(GJGameLevel* level) {
    auto id = linkedMacro(keyFor(level));
    if (!id) {
        return false;
    }
    if (blockwork_select_macro(id->c_str()) != 0) {
        geode::log::warn("blockwork: linked macro '{}' for {} couldn't be selected (deleted?)", *id, keyFor(level));
        return false;
    }
    return true;
}

}
