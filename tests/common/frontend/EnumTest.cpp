#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/frontend/CommonTest.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/Mir.h"
#include "LOICollectionA/frontend/ir/MirSerializer.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/utils/core/Sha256.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;
using LOICollection::utils::Sha256;

namespace {
    /* A realistic two-variant enum: one payload-carrying variant and one
     * payload-free variant. Variants are separated by newlines (no commas
     * required) to exercise the tolerant parser. */
    const std::string kOptionSource = R"(
        enum Option {
            Some(int)
            Nothing
        }
    )";

    MirSerializer::Header headerFor(const std::string& source) {
        MirSerializer::Header header;
        header.scriptId = "enumtest";
        header.abiFingerprint = std::string(32, 'A');
        header.sourceHash = Sha256::compute(source);
        return header;
    }

    std::shared_ptr<MirChunk> compileEnum(const std::string& source, DiagnosticEngine& diagnostics) {
        return compile(source, diagnostics);
    }
}

/* The last expression of a program is returned by eval(); here it is the
 * bound enum value, so we can assert the exact valueToString() rendering. */
TEST(EnumTest, ConstructorWithPayloadToString) {
    EXPECT_EQ(eval(kOptionSource + "\n let a = Some(42)\n a"), "enum#0[0](42)");
}

TEST(EnumTest, PayloadFreeVariantToString) {
    EXPECT_EQ(eval(kOptionSource + "\n let a = Nothing()\n a"), "enum#0[1]");
}

/* Two separately constructed variant values must compare equal by *content*,
 * not by shared_ptr identity. Before the EnumValue::operator== fix two
 * independent Ok(1)-style values compared unequal. */
TEST(EnumTest, EqualityComparesPayloadContents) {
    EXPECT_EQ(eval(kOptionSource + "\n Some(42) == Some(42)"), "true");
    EXPECT_EQ(eval(kOptionSource + "\n Some(42) == Some(43)"), "false");
    EXPECT_EQ(eval(kOptionSource + "\n Some(42) == Nothing()"), "false");
    EXPECT_EQ(eval(kOptionSource + "\n Nothing() == Nothing()"), "true");
}

/* Multi-slot payloads exercise src1..src3 of MAKE_ENUM. */
TEST(EnumTest, MultiplePayloadSlots) {
    EXPECT_EQ(eval("enum Pair { Pt(int, int) }\n let p = Pt(3, 4)\n p"), "enum#0[0](3, 4)");
    EXPECT_EQ(eval("enum Triple { T(int, int, int) }\n let t = T(1, 2, 3)\n t"),
              "enum#0[0](1, 2, 3)");
}

/* Arity must match the declared variant; a missing argument is a hard error. */
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

/* The enum metadata must survive serialize/deserialize intact, and a chunk
 * restored from the blob must still execute to the same value. */
TEST(EnumTest, SerializationRoundTripsEnumMeta) {
    const std::string source = kOptionSource + "\n let a = Some(42)\n a";

    DiagnosticEngine diagnostics;
    auto chunk = compileEnum(source, diagnostics);
    ASSERT_NE(chunk, nullptr);

    ASSERT_EQ(chunk->enums.size(), 1u);
    EXPECT_EQ(chunk->enums[0].name, "Option");
    EXPECT_EQ(chunk->enums[0].variantNames, (std::vector<std::string>{ "Some", "Nothing" }));
    EXPECT_EQ(chunk->enums[0].variantArity, (std::vector<std::uint8_t>{ 1, 0 }));

    auto blob = MirSerializer::serialize(*chunk, headerFor(source));
    ASSERT_TRUE(blob.has_value());

    auto restored = MirSerializer::deserialize(blob.value(), headerFor(source));
    ASSERT_TRUE(restored.has_value());

    EXPECT_EQ(restored->enums.size(), 1u);
    EXPECT_EQ(restored->enums[0].name, "Option");
    EXPECT_EQ(restored->enums[0].variantNames, (std::vector<std::string>{ "Some", "Nothing" }));
    EXPECT_EQ(restored->enums[0].variantArity, (std::vector<std::uint8_t>{ 1, 0 }));

    auto restoredPtr = std::make_shared<MirChunk>(std::move(restored.value()));
    DiagnosticEngine runDiag;
    ir::VM vm(runDiag);
    auto result = vm.run(restoredPtr, {});
    ASSERT_FALSE(runDiag.hasErrors());
    EXPECT_EQ(ir::VM::valueToString(result), "enum#0[0](42)");
}
