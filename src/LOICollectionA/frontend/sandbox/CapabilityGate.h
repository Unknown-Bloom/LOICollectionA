#pragma once

#include <set>

namespace LOICollection::frontend::sandbox {
    enum class ScriptTrustLevel {
        Builtin,
        Admin,
        User,
    };

    enum class Capability {
        GuiRead,
        GuiWrite,
        ServerCommand,
        PlayerDataRead,
        PlayerDataWrite,
        StorageRead,
        StorageWrite,
        Network,
        Filesystem,
    };

    class CapabilityGate {
    public:
        CapabilityGate() = default;
        explicit CapabilityGate(std::set<Capability> granted) : mGranted(std::move(granted)) {}

        static CapabilityGate forTrustLevel(ScriptTrustLevel level) {
            switch (level) {
                case ScriptTrustLevel::Builtin:
                case ScriptTrustLevel::Admin:
                    return CapabilityGate({
                        Capability::GuiRead, Capability::GuiWrite,
                        Capability::ServerCommand,
                        Capability::PlayerDataRead, Capability::PlayerDataWrite,
                        Capability::StorageRead, Capability::StorageWrite,
                    });
                case ScriptTrustLevel::User:
                    return CapabilityGate({ Capability::GuiRead, Capability::GuiWrite });
            }
            return {};
        }

        [[nodiscard]] bool grants(Capability capability) const { return mGranted.contains(capability); }

        void grant(Capability capability) { mGranted.insert(capability); }
        void revoke(Capability capability) { mGranted.erase(capability); }

    private:
        std::set<Capability> mGranted;
    };
}
