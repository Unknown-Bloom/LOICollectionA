#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace LOICollection::frontend::sandbox {

    namespace Config {
        struct C_ScriptCommandPermission {
            bool allow = false;
            std::vector<std::string> templates;
        };

        struct C_ScriptGuiPermission {
            std::vector<std::string> values;
            std::vector<std::string> requests;
            std::vector<std::string> callbacks;
        };

        struct C_ScriptPermissionEntry {
            bool enabled = true;
            C_ScriptCommandPermission commands;
            C_ScriptGuiPermission gui;
        };

        // Mirrors `assets/common/gui/permission.json`.
        struct C_ScriptPermission {
            std::string defaultPolicy = "deny";
            std::unordered_map<std::string, C_ScriptPermissionEntry> scripts;
        };
    }

    // Per-script authorization for host capabilities (`mc::runCmd` and the
    // `GUIManager::{value,request,callback}` business callbacks).  The
    // script id is the file name relative to the `gui` directory, e.g.
    // `"wallet.lcui"`.
    //
    // Note: per the design, `mc::runCmd` keeps its highest server-side
    // permission level when a command template matches; this gate only decides
    // *whether* a script may run a command and *which* commands it may run.
    class PermissionGate {
    public:
        PermissionGate() = default;
        explicit PermissionGate(Config::C_ScriptPermission config) : mConfig(std::move(config)) {}

        static std::optional<PermissionGate> fromJson(const std::string& json, std::string& error) {
            try {
                return PermissionGate(parse(nlohmann::json::parse(json)));
            } catch (const nlohmann::json::exception& e) {
                error = e.what();
                return std::nullopt;
            }
        }

        [[nodiscard]] bool isScriptEnabled(const std::string& scriptId) const {
            if (const auto* entry = find(scriptId))
                return entry->enabled;

            return fallbackAllowed();
        }

        [[nodiscard]] bool isCommandAllowed(
            const std::string& scriptId, const std::string& command, const std::string& playerName
        ) const {
            const auto* entry = find(scriptId);
            if (!entry)
                return fallbackAllowed();
            if (!entry->enabled || !entry->commands.allow)
                return false;
            if (entry->commands.templates.empty())
                return true;   // allow = true with no templates means "any command"

            const std::string resolved = replacePlayer(command, playerName);
            for (const auto& template_ : entry->commands.templates) {
                if (resolved == replacePlayer(template_, playerName))
                    return true;
            }

            return false;
        }

        [[nodiscard]] bool isGuiValueAllowed(const std::string& scriptId, const std::string& id) const {
            return isGuiIdAllowed(scriptId, id, &Config::C_ScriptGuiPermission::values);
        }

        [[nodiscard]] bool isGuiRequestAllowed(const std::string& scriptId, const std::string& id) const {
            return isGuiIdAllowed(scriptId, id, &Config::C_ScriptGuiPermission::requests);
        }

        [[nodiscard]] bool isGuiCallbackAllowed(const std::string& scriptId, const std::string& id) const {
            return isGuiIdAllowed(scriptId, id, &Config::C_ScriptGuiPermission::callbacks);
        }

        // Form navigation (`open` / `switchTo`) is low-risk and stays allowed by
        // default unless the script is disabled or denied.
        [[nodiscard]] bool isGuiNavigationAllowed(const std::string& scriptId) const {
            return isScriptEnabled(scriptId);
        }

    private:
        using GuiList = std::vector<std::string> Config::C_ScriptGuiPermission::*;

        Config::C_ScriptPermission mConfig;

        [[nodiscard]] const Config::C_ScriptPermissionEntry* find(const std::string& scriptId) const {
            const auto it = mConfig.scripts.find(scriptId);
            return it == mConfig.scripts.end() ? nullptr : &it->second;
        }

        [[nodiscard]] bool fallbackAllowed() const { return mConfig.defaultPolicy == "allow"; }

        [[nodiscard]] bool isGuiIdAllowed(const std::string& scriptId, const std::string& id, GuiList list) const {
            const auto* entry = find(scriptId);
            if (!entry)
                return fallbackAllowed();
            if (!entry->enabled)
                return false;

            const auto& ids = entry->gui.*list;
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }

        static std::string replacePlayer(std::string text, const std::string& playerName) {
            constexpr std::string_view placeholder = "${player}";
            for (std::size_t pos = 0; (pos = text.find(placeholder, pos)) != std::string::npos;
                 pos += playerName.size()) {
                text.replace(pos, placeholder.size(), playerName);
            }

            return text;
        }

        static Config::C_ScriptPermission parse(const nlohmann::json& root) {
            Config::C_ScriptPermission result;
            if (root.contains("defaultPolicy") && root["defaultPolicy"].is_string())
                result.defaultPolicy = root["defaultPolicy"].get<std::string>();

            if (!root.contains("scripts") || !root["scripts"].is_object())
                return result;

            for (const auto& [scriptId, value] : root["scripts"].items()) {
                Config::C_ScriptPermissionEntry entry;
                if (value.contains("enabled") && value["enabled"].is_boolean())
                    entry.enabled = value["enabled"].get<bool>();

                if (const auto& commands = value.value("commands", nlohmann::json::object());
                    commands.is_object()) {
                    if (commands.contains("allow") && commands["allow"].is_boolean())
                        entry.commands.allow = commands["allow"].get<bool>();
                    if (commands.contains("templates") && commands["templates"].is_array()) {
                        for (const auto& item : commands["templates"])
                            if (item.is_string())
                                entry.commands.templates.push_back(item.get<std::string>());
                    }
                }

                if (const auto& gui = value.value("gui", nlohmann::json::object()); gui.is_object()) {
                    readList(gui, "values", entry.gui.values);
                    readList(gui, "requests", entry.gui.requests);
                    readList(gui, "callbacks", entry.gui.callbacks);
                }

                result.scripts.emplace(scriptId, std::move(entry));
            }

            return result;
        }

        static void readList(const nlohmann::json& object, const char* key, std::vector<std::string>& out) {
            const auto it = object.find(key);
            if (it == object.end() || !it->is_array())
                return;

            for (const auto& item : *it)
                if (item.is_string())
                    out.push_back(item.get<std::string>());
        }
    };

    // Process-wide permission gate consumed by the builtins.  Defaults to
    // deny-all; the embedding application installs the parsed permission.json
    // through setPermissionGate() before running scripts.
    inline PermissionGate& permissionGate() {
        static PermissionGate gate;
        return gate;
    }

    inline void setPermissionGate(PermissionGate gate) {
        permissionGate() = std::move(gate);
    }
}
