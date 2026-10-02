#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/Mir.h"
#include "LOICollectionA/frontend/ir/MirSerializer.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/utils/core/Sha256.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;
using LOICollection::utils::Sha256;

namespace {
    std::shared_ptr<MirChunk> compileScript(const std::string& source) {
        DiagnosticEngine diagnostics;

        Lexer lexer(source, diagnostics);
        Parser parser(lexer, diagnostics);

        auto ast = parser.parse();
        if (diagnostics.hasErrors())
            return nullptr;

        SemanticAnalyzer analyzer(diagnostics);
        analyzer.analyze(static_cast<ProgramNode&>(*ast));
        if (diagnostics.hasErrors())
            return nullptr;

        auto chunk = std::make_shared<MirChunk>(Compiler(diagnostics).compile(*ast));
        if (diagnostics.hasErrors())
            return nullptr;

        Optimizer optimizer;
        optimizer.optimize(*chunk);

        return chunk;
    }

    std::string runChunk(const std::shared_ptr<MirChunk>& chunk) {
        DiagnosticEngine diagnostics;

        ir::VM vm(diagnostics);
        auto result = vm.run(chunk, {});
        if (diagnostics.hasErrors())
            return {};

        return ir::VM::valueToString(result);
    }

    const std::string kSource = R"(
        let twice = func (n: int) -> int {
            return n * 2;
        };

        let result = twice(21);
        result
    )";

    MirSerializer::Header headerFor(
        const std::string& source,
        const std::vector<std::string>& imports = {}
    ) {
        MirSerializer::Header header;
        header.scriptId = "test";
        header.abiFingerprint = std::string(32, 'A');
        header.sourceHash = Sha256::compute(source);

        for (const auto& import : imports)
            header.importHashes.push_back(Sha256::compute(import));

        return header;
    }

    std::unique_ptr<MirChunk> nestedChunk(int depth) {
        auto root = std::make_unique<MirChunk>();
        root->slotCount = 0;

        MirChunk* tail = root.get();
        for (int i = 0; i < depth; ++i) {
            tail->methodBodies.push_back(std::make_unique<MirChunk>());
            tail = tail->methodBodies.back().get();
        }

        return root;
    }

    int nestingDepth(const MirChunk& chunk) {
        int depth = 0;

        const MirChunk* cursor = &chunk;
        while (!cursor->methodBodies.empty()) {
            cursor = cursor->methodBodies.front().get();
            ++depth;
        }

        return depth;
    }

    ValueNode::ValueType nestedArray(int depth) {
        ValueNode::ValueType value = std::make_shared<ArrayValue>();

        for (int i = 0; i < depth; ++i) {
            auto outer = std::make_shared<ArrayValue>();
            outer->elements.push_back(value);
            value = std::move(outer);
        }

        return value;
    }
}

TEST(MirSerializerTest, RoundTripPreservesBehavior) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    auto blob = MirSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto restored = MirSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    EXPECT_EQ(runChunk(std::make_shared<MirChunk>(std::move(*restored))), "42");
}

TEST(MirSerializerTest, RoundTripPreservesEveryTypeKind) {
    const std::vector<TypeKind> kinds = {
        TypeKind::Unknown, TypeKind::Int, TypeKind::Float, TypeKind::String, TypeKind::Bool,
        TypeKind::Object, TypeKind::Function, TypeKind::Void, TypeKind::Array, TypeKind::Variant,
        TypeKind::Optional, TypeKind::Generic, TypeKind::Trait, TypeKind::None
    };

    MirChunk chunk;
    for (const TypeKind kind : kinds) {
        MirInstr instr;
        instr.op = MirOp::HALT;
        instr.type.kind = kind;
        chunk.code.push_back(instr);
    }

    auto blob = MirSerializer::serialize(chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto restored = MirSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->code.size(), kinds.size());

    for (std::size_t i = 0; i < kinds.size(); ++i)
        EXPECT_EQ(restored->code.at(i).type.kind, kinds.at(i)) << "kind index " << i;
}

TEST(MirSerializerTest, RoundTripPreservesNestedTypeInfo) {
    MirChunk chunk;
    chunk.code.resize(2);

    chunk.code[0].op = MirOp::HALT;
    chunk.code[0].type.kind = TypeKind::Variant;
    chunk.code[0].type.className = "ProbeClass";
    chunk.code[0].type.typeVar = "T";
    chunk.code[0].type.variantOptions.push_back(TypeInfo{ TypeKind::Int });
    chunk.code[0].type.variantOptions.push_back(TypeInfo{ TypeKind::None });

    chunk.code[1].op = MirOp::RETURN;
    chunk.code[1].type.kind = TypeKind::Optional;
    chunk.code[1].type.optionalInner = std::make_shared<TypeInfo>(TypeInfo{ TypeKind::Trait });

    auto blob = MirSerializer::serialize(chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto restored = MirSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->code.size(), 2u);

    const TypeInfo& variant = restored->code.at(0).type;
    EXPECT_EQ(variant.kind, TypeKind::Variant);
    EXPECT_EQ(variant.className, "ProbeClass");
    EXPECT_EQ(variant.typeVar, "T");
    ASSERT_EQ(variant.variantOptions.size(), 2u);
    EXPECT_EQ(variant.variantOptions.at(0).kind, TypeKind::Int);
    EXPECT_EQ(variant.variantOptions.at(1).kind, TypeKind::None);

    const TypeInfo& optional = restored->code.at(1).type;
    EXPECT_EQ(optional.kind, TypeKind::Optional);
    ASSERT_NE(optional.optionalInner, nullptr);
    EXPECT_EQ(optional.optionalInner->kind, TypeKind::Trait);
}

TEST(MirSerializerTest, RoundTripIsStableAcrossCycles) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    auto first = MirSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(first.has_value());

    auto restored = MirSerializer::deserialize(first.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    auto second = MirSerializer::serialize(*restored, headerFor(kSource));
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(first.value(), second.value());
}

TEST(MirSerializerTest, RoundTripWithImportHashes) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    const std::string libSource = "func base() -> int {\n    return 1;\n}";
    auto header = headerFor(kSource, { libSource });

    auto blob = MirSerializer::serialize(*chunk, header);
    ASSERT_TRUE(blob.has_value());

    auto restored = MirSerializer::deserialize(blob.value(), header);
    ASSERT_TRUE(restored.has_value());
}

TEST(MirSerializerTest, PeekHeaderRecoversMetadata) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    auto header = headerFor(kSource);
    auto blob = MirSerializer::serialize(*chunk, header);
    ASSERT_TRUE(blob.has_value());

    auto peeked = MirSerializer::peekHeader(blob.value());
    ASSERT_TRUE(peeked.has_value());

    EXPECT_EQ(peeked->scriptId, header.scriptId);
    EXPECT_EQ(peeked->abiFingerprint, header.abiFingerprint);
    EXPECT_EQ(peeked->sourceHash, header.sourceHash);
}

TEST(MirSerializerTest, RejectsChangedSourceHash) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    auto blob = MirSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto stale = headerFor(kSource + "\n");
    EXPECT_FALSE(MirSerializer::deserialize(blob.value(), stale).has_value());
}

TEST(MirSerializerTest, RejectsChangedImportHash) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    const std::string libSource = "func base() -> int {\n    return 1;\n}";
    auto blob = MirSerializer::serialize(*chunk, headerFor(kSource, { libSource }));
    ASSERT_TRUE(blob.has_value());

    auto stale = headerFor(kSource, { libSource + "\n" });
    EXPECT_FALSE(MirSerializer::deserialize(blob.value(), stale).has_value());
}

TEST(MirSerializerTest, RejectsImportCountMismatch) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    const std::string libSource = "func base() -> int {\n    return 1;\n}";
    auto blob = MirSerializer::serialize(*chunk, headerFor(kSource, { libSource }));
    ASSERT_TRUE(blob.has_value());

    auto stale = headerFor(kSource, { libSource, libSource });
    EXPECT_FALSE(MirSerializer::deserialize(blob.value(), stale).has_value());
}

TEST(MirSerializerTest, RejectsCorruptedBlob) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    auto blob = MirSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    const auto& header = headerFor(kSource);

    EXPECT_FALSE(MirSerializer::deserialize("not a cache blob", header).has_value());

    auto truncated = blob.value().substr(0, blob.value().size() - 1);
    EXPECT_FALSE(MirSerializer::deserialize(truncated, header).has_value());

    auto damaged = blob.value();
    damaged[damaged.size() / 2] = static_cast<char>(damaged[damaged.size() / 2] ^ 0x40);
    EXPECT_FALSE(MirSerializer::deserialize(damaged, header).has_value());
}

TEST(MirSerializerTest, DebugInfoRoundTrip) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    std::string bodyChecksum;
    auto blob = MirSerializer::serialize(*chunk, headerFor(kSource), &bodyChecksum);
    ASSERT_TRUE(blob.has_value());

    auto debugBlob = MirSerializer::serializeDebugInfo(*chunk, bodyChecksum);
    ASSERT_TRUE(debugBlob.has_value());

    auto restored = MirSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    EXPECT_TRUE(MirSerializer::attachDebugInfo(*restored, debugBlob.value(), bodyChecksum));
}

TEST(MirSerializerTest, DebugInfoRejectsMismatchedChecksum) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    std::string bodyChecksum;
    auto blob = MirSerializer::serialize(*chunk, headerFor(kSource), &bodyChecksum);
    ASSERT_TRUE(blob.has_value());

    auto debugBlob = MirSerializer::serializeDebugInfo(*chunk, bodyChecksum);
    ASSERT_TRUE(debugBlob.has_value());

    auto restored = MirSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    const std::string wrongChecksum(32, 'B');
    EXPECT_FALSE(MirSerializer::attachDebugInfo(*restored, debugBlob.value(), wrongChecksum));
}

TEST(MirSerializerTest, AcceptsNestedMethodBodies) {
    auto chunk = nestedChunk(8);

    auto blob = MirSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto restored = MirSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    EXPECT_EQ(nestingDepth(*restored), 8);
}

TEST(MirSerializerTest, RejectsExcessiveNesting) {
    auto chunk = nestedChunk(4096);

    auto blob = MirSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    EXPECT_FALSE(MirSerializer::deserialize(blob.value(), headerFor(kSource)).has_value());
}

TEST(MirSerializerTest, RejectsExcessiveConstantNesting) {
    MirChunk chunk;
    chunk.slotCount = 0;
    chunk.constants.push_back(nestedArray(4096));

    auto blob = MirSerializer::serialize(chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    EXPECT_FALSE(MirSerializer::deserialize(blob.value(), headerFor(kSource)).has_value());

    MirChunk usable;
    usable.slotCount = 0;
    usable.constants.push_back(nestedArray(8));

    auto usableBlob = MirSerializer::serialize(usable, headerFor(kSource));
    ASSERT_TRUE(usableBlob.has_value());

    EXPECT_TRUE(MirSerializer::deserialize(usableBlob.value(), headerFor(kSource)).has_value());
}
