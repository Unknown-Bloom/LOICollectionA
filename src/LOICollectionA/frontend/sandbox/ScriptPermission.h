#pragma once

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "LOICollectionA/base/ServiceProvider.h"
#include "LOICollectionA/frontend/sandbox/ScriptBudget.h"

namespace LOICollection::frontend::sandbox {
    enum class Capability {
        GuiRead,
        GuiWrite,
        ServerCommand,
        PlayerDataRead,
        PlayerDataWrite,
        StorageRead,
        StorageWrite,
    };

    namespace Config {
        struct C_ScriptCommandPermission {
            bool allow = false;
            std::vector<std::string> templates;
        };

        struct C_ScriptGuiPermission {
            std::vector<std::string> values;
            std::vector<std::string> requests;
            std::vector<std::string> callbacks;
            std::vector<std::string> navigations;
        };

        struct C_ScriptPermissionEntry {
            bool enabled = true;
            std::string trust;
            C_ScriptCommandPermission commands;
            C_ScriptGuiPermission gui;
            BudgetOverride budget;
        };

        struct C_ScriptPermission {
            std::string defaultPolicy = "deny";
            std::string defaultTrust = "user";
            std::unordered_map<std::string, std::vector<std::string>> trustLevels;
            BudgetOverride defaultBudget;
            std::unordered_map<std::string, C_ScriptPermissionEntry> scripts;
        };
    }

    class PermissionGate {
    public:
        PermissionGate() = default;
        explicit PermissionGate(Config::C_ScriptPermission config) : mConfig(std::move(config)) {
            mBudget.setDefault(mConfig.defaultBudget);
            mDefaultCaps = resolveCaps(mConfig.defaultTrust);
            for (const auto& [scriptId, entry] : mConfig.scripts) {
                mBudget.setOverride(scriptId, entry.budget);
                mCaps.emplace(scriptId, resolveCaps(entry.trust.empty() ? mConfig.defaultTrust : entry.trust));
            }
        }

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
                return fallbackAllowed() && hasCapability(scriptId, Capability::ServerCommand);
            if (!entry->enabled || !hasCapability(scriptId, Capability::ServerCommand))
                return false;
            if (!entry->commands.allow || entry->commands.templates.empty())
                return false;

            return std::find(entry->commands.templates.begin(), entry->commands.templates.end(), command)
                != entry->commands.templates.end();
        }

        [[nodiscard]] bool isGuiValueAllowed(const std::string& scriptId, const std::string& id) const {
            return isGuiIdAllowed(scriptId, id, &Config::C_ScriptGuiPermission::values, Capability::GuiRead);
        }

        [[nodiscard]] bool isGuiRequestAllowed(const std::string& scriptId, const std::string& id) const {
            return isGuiIdAllowed(scriptId, id, &Config::C_ScriptGuiPermission::requests, Capability::GuiRead);
        }

        [[nodiscard]] bool isGuiCallbackAllowed(const std::string& scriptId, const std::string& id) const {
            return isGuiIdAllowed(scriptId, id, &Config::C_ScriptGuiPermission::callbacks, Capability::GuiWrite);
        }

        [[nodiscard]] bool isGuiNavigationAllowed(const std::string& scriptId) const {
            return isScriptEnabled(scriptId) && hasCapability(scriptId, Capability::GuiWrite);
        }

        [[nodiscard]] bool isGuiNavigationTargetAllowed(
            const std::string& scriptId, const std::string& targetScript, const std::string& formId
        ) const {
            if (targetScript == scriptId)
                return true;

            const auto* entry = find(scriptId);
            if (!entry)
                return fallbackAllowed() && hasCapability(scriptId, Capability::GuiWrite);
            if (!entry->enabled || !hasCapability(scriptId, Capability::GuiWrite))
                return false;

            const auto& targets = entry->gui.navigations;
            return std::any_of(targets.begin(), targets.end(), [&](const std::string& target) {
                return target == targetScript || target == formId;
            });
        }

        [[nodiscard]] bool hasEntry(const std::string& scriptId) const {
            return find(scriptId) != nullptr;
        }

        [[nodiscard]] SandboxBudget budgetFor(const std::string& scriptId) const {
            return mBudget.resolve(scriptId);
        }

        [[nodiscard]] bool hasCapability(const std::string& scriptId, Capability capability) const {
            if (const auto it = mCaps.find(scriptId); it != mCaps.end())
                return it->second.contains(capability);

            return mDefaultCaps.contains(capability);
        }

    private:
        using GuiList = std::vector<std::string> Config::C_ScriptGuiPermission::*;

        Config::C_ScriptPermission mConfig;
        BudgetPolicy mBudget;
        std::unordered_map<std::string, std::set<Capability>> mCaps;
        std::set<Capability> mDefaultCaps;

        [[nodiscard]] const Config::C_ScriptPermissionEntry* find(const std::string& scriptId) const {
            const auto it = mConfig.scripts.find(scriptId);
            return it == mConfig.scripts.end() ? nullptr : &it->second;
        }

        [[nodiscard]] bool fallbackAllowed() const { return mConfig.defaultPolicy == "allow"; }

        [[nodiscard]] std::set<Capability> resolveCaps(const std::string& trustName) const {
            std::set<Capability> caps;
            const auto it = mConfig.trustLevels.find(trustName);
            if (it == mConfig.trustLevels.end())
                return caps;

            for (const auto& name : it->second)
                if (const auto cap = capabilityFromName(name))
                    caps.insert(*cap);

            return caps;
        }

        [[nodiscard]] bool isGuiIdAllowed(
            const std::string& scriptId, const std::string& id, GuiList list, Capability cap
        ) const {
            const auto* entry = find(scriptId);
            if (!entry)
                return fallbackAllowed() && hasCapability(scriptId, cap);
            if (!entry->enabled || !hasCapability(scriptId, cap))
                return false;

            const auto& ids = entry->gui.*list;
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }

        static std::optional<Capability> capabilityFromName(const std::string& name) {
            if (name == "GuiRead") return Capability::GuiRead;
            if (name == "GuiWrite") return Capability::GuiWrite;
            if (name == "ServerCommand") return Capability::ServerCommand;
            if (name == "PlayerDataRead") return Capability::PlayerDataRead;
            if (name == "PlayerDataWrite") return Capability::PlayerDataWrite;
            if (name == "StorageRead") return Capability::StorageRead;
            if (name == "StorageWrite") return Capability::StorageWrite;
            return std::nullopt;
        }

        static Config::C_ScriptPermission parse(const nlohmann::json& root) {
            Config::C_ScriptPermission result;
            if (root.contains("defaultPolicy") && root["defaultPolicy"].is_string())
                result.defaultPolicy = root["defaultPolicy"].get<std::string>();
            if (root.contains("defaultTrust") && root["defaultTrust"].is_string())
                result.defaultTrust = root["defaultTrust"].get<std::string>();
            if (root.contains("trustLevels") && root["trustLevels"].is_object())
                for (const auto& [name, value] : root["trustLevels"].items())
                    if (value.is_array())
                        for (const auto& item : value)
                            if (item.is_string())
                                result.trustLevels[name].push_back(item.get<std::string>());

            result.defaultBudget = readBudget(root.value("budget", nlohmann::json::object()));

            if (!root.contains("scripts") || !root["scripts"].is_object())
                return result;

            for (const auto& [scriptId, value] : root["scripts"].items()) {
                Config::C_ScriptPermissionEntry entry;
                if (value.contains("enabled") && value["enabled"].is_boolean())
                    entry.enabled = value["enabled"].get<bool>();
                if (value.contains("trust") && value["trust"].is_string())
                    entry.trust = value["trust"].get<std::string>();

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
                    readList(gui, "navigations", entry.gui.navigations);
                }

                entry.budget = readBudget(value.value("budget", nlohmann::json::object()));

                result.scripts.emplace(scriptId, std::move(entry));
            }

            return result;
        }

        static BudgetOverride readBudget(const nlohmann::json& object) {
            BudgetOverride result;
            if (!object.is_object())
                return result;

            result.maxInstructions = readCount(object, "maxInstructions");
            result.maxFrames = readCount(object, "maxFrames");
            result.maxNativeCalls = readCount(object, "maxNativeCalls");
            result.maxObjectCount = readCount(object, "maxObjectCount");
            result.maxArrayElements = readCount(object, "maxArrayElements");
            result.maxStringBytes = readCount(object, "maxStringBytes");
            result.maxTotalBytes = readCount(object, "maxTotalBytes");

            if (const auto milliseconds = readCount(object, "maxWallTimeMs"))
                result.maxWallTime = std::chrono::milliseconds(*milliseconds);

            return result;
        }

        static std::optional<std::size_t> readCount(const nlohmann::json& object, const char* key) {
            const auto it = object.find(key);
            if (it == object.end() || !it->is_number_unsigned())
                return std::nullopt;

            return it->get<std::size_t>();
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

    [[nodiscard]] inline SandboxBudget budgetForScript(const std::string& scriptId) {
        if (const auto service = ServiceProvider::getInstance().getService<ScriptPermissionService>())
            return service->gate().budgetFor(scriptId);

        return SandboxBudget{};
    }
}
