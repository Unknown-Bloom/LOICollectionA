#pragma once

#include <set>

namespace LOICollection::frontend::sandbox {

    enum class ScriptTrustLevel {
        Builtin,   // shipped with the plugin / developer-signed
        Admin,     // written by an administrator
        User,      // uploaded by players (most restricted)
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

    // deny-by-default capability gate.  A script instance carries a set of
    // granted capabilities; builtin call sites consult it before dispatch.
    class CapabilityGate {
    public:
        CapabilityGate() = default;
        explicit CapabilityGate(std::set<Capability> granted) : mGranted(std::move(granted)) {}

        static CapabilityGate forTrustLevel(ScriptTrustLevel level) {
            switch (level) {
                case ScriptTrustLevel::Builtin:
                    return CapabilityGate({
                        Capability::GuiRead, Capability::GuiWrite,
                        Capability::ServerCommand,
                        Capability::PlayerDataRead, Capability::PlayerDataWrite,
                        Capability::StorageRead, Capability::StorageWrite,
                    });
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
