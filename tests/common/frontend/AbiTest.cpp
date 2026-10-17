#include <gtest/gtest.h>

#include <string>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/ir/Abi.h"
#include "LOICollectionA/frontend/ir/Mir.h"
#include "LOICollectionA/utils/core/Sha256.h"

using namespace LOICollection::frontend;
using LOICollection::frontend::ir::abiFingerprint;
using LOICollection::frontend::ir::ClassMeta;
using LOICollection::frontend::ir::layoutFingerprint;
using LOICollection::frontend::ir::MethodMeta;
using LOICollection::frontend::ir::MirChunk;
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

TEST(AbiTest, LayoutFingerprintTracksTheClassTable) {
    MirChunk chunk;

    ClassMeta node;
    node.name = "Node";
    node.fieldNames = { "next", "value" };
    node.hasDefault = { true, false };
    chunk.classes.push_back(node);

    const std::string before = layoutFingerprint(chunk);
    EXPECT_EQ(before.size(), 32u);
    EXPECT_EQ(layoutFingerprint(chunk), before);

    chunk.classes.front().fieldNames = { "value", "next" };
    const std::string reordered = layoutFingerprint(chunk);
    EXPECT_NE(before, reordered);

    chunk.classes.front().fieldNames.push_back("extra");
    chunk.classes.front().hasDefault.push_back(false);
    EXPECT_NE(reordered, layoutFingerprint(chunk));
}

TEST(AbiTest, LayoutFingerprintCoversMethodsAndBodies) {
    MirChunk chunk;
    chunk.slotCount = 4;
    chunk.methods.push_back(MethodMeta{ "run", 1, -1, 0 });

    const std::string before = layoutFingerprint(chunk);

    chunk.methods.front().argCount = 2;
    const std::string widened = layoutFingerprint(chunk);
    EXPECT_NE(before, widened);

    chunk.methodBodies.push_back(std::make_unique<MirChunk>());
    chunk.methodBodies.back()->slotCount = 9;
    EXPECT_NE(widened, layoutFingerprint(chunk));
}
