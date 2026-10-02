#include <gtest/gtest.h>

#include <string>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/ir/Abi.h"
#include "LOICollectionA/utils/core/Sha256.h"

using namespace LOICollection::frontend;
using LOICollection::frontend::ir::abiFingerprint;
using LOICollection::utils::Sha256;

TEST(AbiTest, FingerprintIsStableAndThirtyTwoBytes) {
    const std::string first = abiFingerprint();
    const std::string second = abiFingerprint();

    EXPECT_EQ(first.size(), 32u);
    EXPECT_EQ(first, second);
}

TEST(AbiTest, FingerprintMatchesTheExportedSurface) {
    const std::string expected = Sha256::compute(
        FunctionCall::getInstance().exportShape()
        + MacroCall::getInstance().exportShape()
        + ClassCall::getInstance().exportShape()
    );

    EXPECT_EQ(abiFingerprint(), expected);
}

TEST(AbiTest, ExportedShapesAreStable) {
    const std::string functions = FunctionCall::getInstance().exportShape();
    const std::string macros = MacroCall::getInstance().exportShape();
    const std::string classes = ClassCall::getInstance().exportShape();

    EXPECT_FALSE(functions.empty());
    EXPECT_FALSE(classes.empty());

    EXPECT_EQ(FunctionCall::getInstance().exportShape(), functions);
    EXPECT_EQ(MacroCall::getInstance().exportShape(), macros);
    EXPECT_EQ(ClassCall::getInstance().exportShape(), classes);
}

TEST(AbiTest, ClassShapeIncludesRegisteredMembers) {
    const std::string shape = ClassCall::getInstance().exportShape();

    EXPECT_TRUE(ClassCall::getInstance().hasField("Map", "keys"));
    EXPECT_NE(shape.find("field Map.keys"), std::string::npos);
}

TEST(AbiTest, RegisteringANativeClassChangesTheFingerprint) {
    const std::string before = abiFingerprint();
    const std::string shapeBefore = ClassCall::getInstance().exportShape();

    ClassCall::getInstance().registerClass("AbiProbeClass", { "probeField" });

    const std::string after = abiFingerprint();
    const std::string shapeAfter = ClassCall::getInstance().exportShape();

    EXPECT_TRUE(ClassCall::getInstance().isRegistered("AbiProbeClass"));
    EXPECT_TRUE(ClassCall::getInstance().hasField("AbiProbeClass", "probeField"));
    EXPECT_EQ(shapeBefore.find("AbiProbeClass"), std::string::npos);
    EXPECT_NE(shapeAfter.find("field AbiProbeClass.probeField"), std::string::npos);
    EXPECT_NE(before, after);
    EXPECT_EQ(after, abiFingerprint());
}
