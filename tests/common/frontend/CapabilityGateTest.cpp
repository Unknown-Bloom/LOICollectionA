#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "LOICollectionA/frontend/sandbox/CapabilityGate.h"

using namespace LOICollection::frontend::sandbox;

namespace capability_gate_test_support {
    const std::vector<Capability> allCapabilities = {
        Capability::GuiRead,
        Capability::GuiWrite,
        Capability::ServerCommand,
        Capability::PlayerDataRead,
        Capability::PlayerDataWrite,
        Capability::StorageRead,
        Capability::StorageWrite,
        Capability::Network,
        Capability::Filesystem,
    };

    std::vector<bool> grantedOf(const CapabilityGate& gate) {
        std::vector<bool> granted;
        granted.reserve(allCapabilities.size());

        for (const Capability capability : allCapabilities)
            granted.push_back(gate.grants(capability));

        return granted;
    }
}

TEST(CapabilityGateTest, DefaultGateGrantsNothing) {
    const CapabilityGate gate;

    EXPECT_EQ(
        capability_gate_test_support::grantedOf(gate),
        std::vector<bool>({ false, false, false, false, false, false, false, false, false })
    );
}

TEST(CapabilityGateTest, BuiltinTrustGrantsEverythingExceptNetworkAndFilesystem) {
    const CapabilityGate gate = CapabilityGate::forTrustLevel(ScriptTrustLevel::Builtin);

    EXPECT_EQ(
        capability_gate_test_support::grantedOf(gate),
        std::vector<bool>({ true, true, true, true, true, true, true, false, false })
    );
}

TEST(CapabilityGateTest, AdminTrustMatchesBuiltinTrust) {
    const CapabilityGate builtin = CapabilityGate::forTrustLevel(ScriptTrustLevel::Builtin);
    const CapabilityGate admin = CapabilityGate::forTrustLevel(ScriptTrustLevel::Admin);

    EXPECT_EQ(capability_gate_test_support::grantedOf(admin), capability_gate_test_support::grantedOf(builtin));
}

TEST(CapabilityGateTest, UserTrustIsLimitedToGui) {
    const CapabilityGate gate = CapabilityGate::forTrustLevel(ScriptTrustLevel::User);

    EXPECT_EQ(
        capability_gate_test_support::grantedOf(gate),
        std::vector<bool>({ true, true, false, false, false, false, false, false, false })
    );
}

TEST(CapabilityGateTest, ConstructorTakesTheExactGrantSet) {
    const CapabilityGate gate({ Capability::Network, Capability::StorageRead });

    EXPECT_TRUE(gate.grants(Capability::Network));
    EXPECT_TRUE(gate.grants(Capability::StorageRead));
    EXPECT_FALSE(gate.grants(Capability::StorageWrite));
    EXPECT_FALSE(gate.grants(Capability::GuiRead));
}

TEST(CapabilityGateTest, GrantAndRevokeMutateTheSet) {
    CapabilityGate gate;

    gate.grant(Capability::Filesystem);
    EXPECT_TRUE(gate.grants(Capability::Filesystem));

    gate.grant(Capability::Filesystem);
    EXPECT_TRUE(gate.grants(Capability::Filesystem));

    gate.revoke(Capability::Filesystem);
    EXPECT_FALSE(gate.grants(Capability::Filesystem));

    gate.revoke(Capability::Filesystem);
    EXPECT_FALSE(gate.grants(Capability::Filesystem));
}

TEST(CapabilityGateTest, RevokeCanNarrowADerivedTrustLevel) {
    CapabilityGate gate = CapabilityGate::forTrustLevel(ScriptTrustLevel::Admin);

    EXPECT_TRUE(gate.grants(Capability::ServerCommand));
    gate.revoke(Capability::ServerCommand);
    EXPECT_FALSE(gate.grants(Capability::ServerCommand));
    EXPECT_TRUE(gate.grants(Capability::StorageWrite));
}

TEST(CapabilityGateTest, GrantCanWidenAUserGate) {
    CapabilityGate gate = CapabilityGate::forTrustLevel(ScriptTrustLevel::User);

    EXPECT_FALSE(gate.grants(Capability::StorageWrite));
    gate.grant(Capability::StorageWrite);
    EXPECT_TRUE(gate.grants(Capability::StorageWrite));
    EXPECT_FALSE(gate.grants(Capability::StorageRead));
}
