#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "LOICollectionA/frontend/ir/Abi.h"
#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/Lir.h"
#include "LOICollectionA/frontend/ir/ChunkSerializer.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/utils/core/Sha256.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;
using LOICollection::frontend::ir::layoutFingerprint;
using LOICollection::utils::Sha256;

namespace {
    std::shared_ptr<Chunk> compileScript(const std::string& source) {
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

        auto chunk = std::make_shared<Chunk>(Compiler(diagnostics).compile(*ast));
        if (diagnostics.hasErrors())
            return nullptr;

        Optimizer optimizer;
        optimizer.optimize(*chunk);

        return chunk;
    }

    std::string runChunk(const std::shared_ptr<Chunk>& chunk) {
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

    ChunkSerializer::Header headerFor(
        const std::string& source,
        const std::vector<std::string>& imports = {}
    ) {
        ChunkSerializer::Header header;
        header.scriptId = "test";
        header.abiFingerprint = std::string(32, 'A');
        header.sourceHash = Sha256::compute(source);

        for (const auto& import : imports)
            header.importHashes.push_back(Sha256::compute(import));

        return header;
    }

    std::unique_ptr<Chunk> nestedChunk(int depth) {
        auto root = std::make_unique<Chunk>();
        root->slotCount = 0;

        Chunk* tail = root.get();
        for (int i = 0; i < depth; ++i) {
            tail->methodBodies.push_back(std::make_unique<Chunk>());
            tail = tail->methodBodies.back().get();
        }

        return root;
    }

    int nestingDepth(const Chunk& chunk) {
        int depth = 0;

        const Chunk* cursor = &chunk;
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

TEST(ChunkSerializerTest, RoundTripPreservesBehavior) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    auto blob = ChunkSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto restored = ChunkSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    EXPECT_EQ(runChunk(std::make_shared<Chunk>(std::move(*restored))), "42");
}

TEST(ChunkSerializerTest, RoundTripPreservesTheLayoutFingerprint) {
    const std::string source = R"(
        class Node {
            public:
                next = 0;
                func value() -> int { return 1; }
        }
        let node = new Node();
        node.value()
    )";

    auto chunk = compileScript(source);
    ASSERT_NE(chunk, nullptr);

    auto blob = ChunkSerializer::serialize(*chunk, headerFor(source));
    ASSERT_TRUE(blob.has_value());

    auto restored = ChunkSerializer::deserialize(blob.value(), headerFor(source));
    ASSERT_TRUE(restored.has_value());

    EXPECT_EQ(layoutFingerprint(*restored), layoutFingerprint(*chunk));
}

TEST(ChunkSerializerTest, RejectsAForgedLayoutDigest) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    auto serialized = ChunkSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(serialized.has_value());

    constexpr std::size_t kHeaderSize = 4 + 4 + 4 + 4 + 32 + 1 + 32 + 4;
    constexpr std::size_t kDigestSize = 32;
    constexpr std::size_t kChecksumAt = kHeaderSize;
    constexpr std::size_t kBodyAt = kHeaderSize + kDigestSize;

    std::string blob = *serialized;
    ASSERT_GT(blob.size(), kBodyAt + kDigestSize);

    std::string body = blob.substr(kBodyAt);
    body[0] = static_cast<char>(body[0] ^ 0x7f);

    blob.replace(kChecksumAt, kDigestSize, Sha256::compute(body));
    blob.replace(kBodyAt, body.size(), body);

    EXPECT_FALSE(ChunkSerializer::deserialize(blob, headerFor(kSource)).has_value());
}

TEST(ChunkSerializerTest, RoundTripPreservesEveryTypeKind) {
    const std::vector<TypeKind> kinds = {
        TypeKind::Unknown, TypeKind::Int, TypeKind::Float, TypeKind::String, TypeKind::Bool,
        TypeKind::Object, TypeKind::Function, TypeKind::Void, TypeKind::Array, TypeKind::Variant,
        TypeKind::Optional, TypeKind::Generic, TypeKind::Trait, TypeKind::None
    };

    Chunk chunk;
    for (const TypeKind kind : kinds) {
        Instr instr;
        instr.op = Op::HALT;
        instr.type.kind = kind;
        chunk.code.push_back(instr);
    }

    auto blob = ChunkSerializer::serialize(chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto restored = ChunkSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->code.size(), kinds.size());

    for (std::size_t i = 0; i < kinds.size(); ++i)
        EXPECT_EQ(restored->code.at(i).type.kind, kinds.at(i)) << "kind index " << i;
}

TEST(ChunkSerializerTest, RoundTripPreservesNestedTypeInfo) {
    Chunk chunk;
    chunk.code.resize(2);

    chunk.code[0].op = Op::HALT;
    chunk.code[0].type.kind = TypeKind::Variant;
    chunk.code[0].type.className = "ProbeClass";
    chunk.code[0].type.typeVar = "T";
    chunk.code[0].type.variantOptions.push_back(TypeInfo{ TypeKind::Int });
    chunk.code[0].type.variantOptions.push_back(TypeInfo{ TypeKind::None });

    chunk.code[1].op = Op::RETURN;
    chunk.code[1].type.kind = TypeKind::Optional;
    chunk.code[1].type.optionalInner = std::make_shared<TypeInfo>(TypeInfo{ TypeKind::Trait });

    auto blob = ChunkSerializer::serialize(chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto restored = ChunkSerializer::deserialize(blob.value(), headerFor(kSource));
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

TEST(ChunkSerializerTest, RoundTripIsStableAcrossCycles) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    auto first = ChunkSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(first.has_value());

    auto restored = ChunkSerializer::deserialize(first.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    auto second = ChunkSerializer::serialize(*restored, headerFor(kSource));
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(first.value(), second.value());
}

TEST(ChunkSerializerTest, RoundTripWithImportHashes) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    const std::string libSource = "func base() -> int {\n    return 1;\n}";
    auto header = headerFor(kSource, { libSource });

    auto blob = ChunkSerializer::serialize(*chunk, header);
    ASSERT_TRUE(blob.has_value());

    auto restored = ChunkSerializer::deserialize(blob.value(), header);
    ASSERT_TRUE(restored.has_value());
}

TEST(ChunkSerializerTest, PeekHeaderRecoversMetadata) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    auto header = headerFor(kSource);
    auto blob = ChunkSerializer::serialize(*chunk, header);
    ASSERT_TRUE(blob.has_value());

    auto peeked = ChunkSerializer::peekHeader(blob.value());
    ASSERT_TRUE(peeked.has_value());

    EXPECT_EQ(peeked->scriptId, header.scriptId);
    EXPECT_EQ(peeked->abiFingerprint, header.abiFingerprint);
    EXPECT_EQ(peeked->sourceHash, header.sourceHash);
}

TEST(ChunkSerializerTest, RejectsChangedSourceHash) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    auto blob = ChunkSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto stale = headerFor(kSource + "\n");
    EXPECT_FALSE(ChunkSerializer::deserialize(blob.value(), stale).has_value());
}

TEST(ChunkSerializerTest, RejectsChangedImportHash) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    const std::string libSource = "func base() -> int {\n    return 1;\n}";
    auto blob = ChunkSerializer::serialize(*chunk, headerFor(kSource, { libSource }));
    ASSERT_TRUE(blob.has_value());

    auto stale = headerFor(kSource, { libSource + "\n" });
    EXPECT_FALSE(ChunkSerializer::deserialize(blob.value(), stale).has_value());
}

TEST(ChunkSerializerTest, RejectsImportCountMismatch) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    const std::string libSource = "func base() -> int {\n    return 1;\n}";
    auto blob = ChunkSerializer::serialize(*chunk, headerFor(kSource, { libSource }));
    ASSERT_TRUE(blob.has_value());

    auto stale = headerFor(kSource, { libSource, libSource });
    EXPECT_FALSE(ChunkSerializer::deserialize(blob.value(), stale).has_value());
}

TEST(ChunkSerializerTest, RejectsCorruptedBlob) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    auto blob = ChunkSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    const auto& header = headerFor(kSource);

    EXPECT_FALSE(ChunkSerializer::deserialize("not a cache blob", header).has_value());

    auto truncated = blob.value().substr(0, blob.value().size() - 1);
    EXPECT_FALSE(ChunkSerializer::deserialize(truncated, header).has_value());

    auto damaged = blob.value();
    damaged[damaged.size() / 2] = static_cast<char>(damaged[damaged.size() / 2] ^ 0x40);
    EXPECT_FALSE(ChunkSerializer::deserialize(damaged, header).has_value());
}

TEST(ChunkSerializerTest, DebugInfoRoundTrip) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    std::string bodyChecksum;
    auto blob = ChunkSerializer::serialize(*chunk, headerFor(kSource), &bodyChecksum);
    ASSERT_TRUE(blob.has_value());

    auto debugBlob = ChunkSerializer::serializeDebugInfo(*chunk, bodyChecksum);
    ASSERT_TRUE(debugBlob.has_value());

    auto restored = ChunkSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    EXPECT_TRUE(ChunkSerializer::attachDebugInfo(*restored, debugBlob.value(), bodyChecksum));
}

TEST(ChunkSerializerTest, DebugInfoRejectsMismatchedChecksum) {
    auto chunk = compileScript(kSource);
    ASSERT_NE(chunk, nullptr);

    std::string bodyChecksum;
    auto blob = ChunkSerializer::serialize(*chunk, headerFor(kSource), &bodyChecksum);
    ASSERT_TRUE(blob.has_value());

    auto debugBlob = ChunkSerializer::serializeDebugInfo(*chunk, bodyChecksum);
    ASSERT_TRUE(debugBlob.has_value());

    auto restored = ChunkSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    const std::string wrongChecksum(32, 'B');
    EXPECT_FALSE(ChunkSerializer::attachDebugInfo(*restored, debugBlob.value(), wrongChecksum));
}

TEST(ChunkSerializerTest, AcceptsNestedMethodBodies) {
    auto chunk = nestedChunk(8);

    auto blob = ChunkSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto restored = ChunkSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    EXPECT_EQ(nestingDepth(*restored), 8);
}

TEST(ChunkSerializerTest, RejectsExcessiveNesting) {
    auto chunk = nestedChunk(4096);

    auto blob = ChunkSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    EXPECT_FALSE(ChunkSerializer::deserialize(blob.value(), headerFor(kSource)).has_value());
}

TEST(ChunkSerializerTest, RejectsExcessiveConstantNesting) {
    Chunk chunk;
    chunk.slotCount = 0;
    chunk.constants.push_back(nestedArray(4096));

    auto blob = ChunkSerializer::serialize(chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    EXPECT_FALSE(ChunkSerializer::deserialize(blob.value(), headerFor(kSource)).has_value());

    Chunk usable;
    usable.slotCount = 0;
    usable.constants.push_back(nestedArray(8));

    auto usableBlob = ChunkSerializer::serialize(usable, headerFor(kSource));
    ASSERT_TRUE(usableBlob.has_value());

    EXPECT_TRUE(ChunkSerializer::deserialize(usableBlob.value(), headerFor(kSource)).has_value());
}
