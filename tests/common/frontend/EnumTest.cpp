#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/frontend/CommonTest.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/Lir.h"
#include "LOICollectionA/frontend/ir/ChunkSerializer.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/utils/core/Sha256.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;
using LOICollection::utils::Sha256;

namespace {
    const std::string kOptionSource = R"(
        enum Option {
            Some(int)
            Nothing
        }
    )";

    ChunkSerializer::Header headerFor(const std::string& source) {
        ChunkSerializer::Header header;
        header.scriptId = "enumtest";
        header.abiFingerprint = std::string(32, 'A');
        header.sourceHash = Sha256::compute(source);
        return header;
    }

    std::shared_ptr<Chunk> compileEnum(const std::string& source, DiagnosticEngine& diagnostics) {
        return compile(source, diagnostics);
    }
}

TEST(EnumTest, ConstructorWithPayloadToString) {
    EXPECT_EQ(eval(kOptionSource + "\n let a = Some(42)\n a"), "enum#0[0](42)");
}

TEST(EnumTest, PayloadFreeVariantToString) {
    EXPECT_EQ(eval(kOptionSource + "\n let a = Nothing()\n a"), "enum#0[1]");
}

TEST(EnumTest, EqualityComparesPayloadContents) {
    EXPECT_EQ(eval(kOptionSource + "\n Some(42) == Some(42)"), "true");
    EXPECT_EQ(eval(kOptionSource + "\n Some(42) == Some(43)"), "false");
    EXPECT_EQ(eval(kOptionSource + "\n Some(42) == Nothing()"), "false");
    EXPECT_EQ(eval(kOptionSource + "\n Nothing() == Nothing()"), "true");
}

TEST(EnumTest, MultiplePayloadSlots) {
    EXPECT_EQ(eval("enum Pair { Pt(int, int) }\n let p = Pt(3, 4)\n p"), "enum#0[0](3, 4)");
    EXPECT_EQ(eval("enum Triple { T(int, int, int) }\n let t = T(1, 2, 3)\n t"),
              "enum#0[0](1, 2, 3)");
}

TEST(EnumTest, ArityMismatchIsRejected) {
    DiagnosticEngine diagnostics;
    auto mir = compileEnum(kOptionSource + "\n let a = Some()\n a", diagnostics);
    EXPECT_EQ(mir, nullptr);
    EXPECT_TRUE(diagnostics.hasErrors());
}

TEST(EnumTest, TooManyArgumentsIsRejected) {
    DiagnosticEngine diagnostics;
    auto mir = compileEnum(kOptionSource + "\n let a = Nothing(1)\n a", diagnostics);
    EXPECT_EQ(mir, nullptr);
    EXPECT_TRUE(diagnostics.hasErrors());
}

TEST(EnumTest, SerializationRoundTripsEnumMeta) {
    const std::string source = kOptionSource + "\n let a = Some(42)\n a";

    DiagnosticEngine diagnostics;
    auto chunk = compileEnum(source, diagnostics);
    ASSERT_NE(chunk, nullptr);

    ASSERT_EQ(chunk->enums.size(), 1u);
    EXPECT_EQ(chunk->enums[0].name, "Option");
    EXPECT_EQ(chunk->enums[0].variantNames, (std::vector<std::string>{ "Some", "Nothing" }));
    EXPECT_EQ(chunk->enums[0].variantArity, (std::vector<std::uint8_t>{ 1, 0 }));

    auto blob = ChunkSerializer::serialize(*chunk, headerFor(source));
    ASSERT_TRUE(blob.has_value());

    auto restored = ChunkSerializer::deserialize(blob.value(), headerFor(source));
    ASSERT_TRUE(restored.has_value());

    EXPECT_EQ(restored->enums.size(), 1u);
    EXPECT_EQ(restored->enums[0].name, "Option");
    EXPECT_EQ(restored->enums[0].variantNames, (std::vector<std::string>{ "Some", "Nothing" }));
    EXPECT_EQ(restored->enums[0].variantArity, (std::vector<std::uint8_t>{ 1, 0 }));

    auto restoredPtr = std::make_shared<Chunk>(std::move(restored.value()));
    DiagnosticEngine runDiag;
    ir::VM vm(runDiag);
    auto result = vm.run(restoredPtr, {});
    ASSERT_FALSE(runDiag.hasErrors());
    EXPECT_EQ(ir::VM::valueToString(result), "enum#0[0](42)");
}
