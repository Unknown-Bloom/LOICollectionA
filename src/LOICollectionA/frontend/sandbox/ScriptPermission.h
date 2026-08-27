#pragma once

#include <algorithm>
#include <optional>
#include <string>
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

        struct C_ScriptPermission {
            std::string defaultPolicy = "deny";
            std::unordered_map<std::string, C_ScriptPermissionEntry> scripts;
        };
    }

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

        [[nodiscard]] bool isCommandAllowed(const std::string& scriptId, const std::string& command) const {
            const auto* entry = find(scriptId);
            if (!entry)
                return fallbackAllowed();
            if (!entry->enabled || !entry->commands.allow || entry->commands.templates.empty())
                return false;

            return std::find(entry->commands.templates.begin(), entry->commands.templates.end(), command)
                != entry->commands.templates.end();
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

    class ScriptPermissionService {
    public:
        void setGate(PermissionGate gate) { mGate = std::move(gate); }
        [[nodiscard]] const PermissionGate& gate() const { return mGate; }

    private:
        PermissionGate mGate;
    };
}
