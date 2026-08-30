#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"
#include "LOICollectionA/frontend/ir/Abi.h"
#include "LOICollectionA/frontend/ir/BytecodeSerializer.h"
#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/utils/core/Sha256.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;
using LOICollection::utils::Sha256;

namespace {
    std::shared_ptr<BytecodeChunk> compileScript(const std::string& source) {
        DiagnosticEngine diagnostics;

        Lexer lexer(source, diagnostics);
        Parser parser(lexer, diagnostics);

        auto ast = parser.parse();
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        SemanticAnalyzer analyzer(diagnostics);
        analyzer.analyze(static_cast<ProgramNode&>(*ast));
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        auto bytecode = std::make_shared<BytecodeChunk>(Compiler(diagnostics).compile(*ast));
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        Optimizer optimizer;
        optimizer.optimize(*bytecode);

        return bytecode;
    }

    std::string runChunk(const std::shared_ptr<BytecodeChunk>& chunk) {
        DiagnosticEngine diagnostics;

        ir::VM vm(diagnostics);
        auto result = vm.run(chunk, {});
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        return ir::VM::valueToString(result);
    }

    const std::string kSource = R"(
        let twice = func (n) -> int {
            return n * 2;
        };

        let result = twice(21);
    )";

    BytecodeSerializer::Header headerFor(
        const std::string& source,
        const std::vector<std::string>& imports = {}
    ) {
        BytecodeSerializer::Header header;
        header.abiFingerprint = abiFingerprint();
        header.sourceHash = Sha256::compute(source);

        for (const auto& import : imports)
            header.importHashes.push_back(Sha256::compute(import));

        return header;
    }
}

TEST(Sha256Test, KnownVectors) {
    const std::string abcDigest(
        "\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae"
        "\x22\x23\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad",
        32
    );

    EXPECT_EQ(Sha256::compute("abc"), abcDigest);
    EXPECT_EQ(
        Sha256::compute(""),
        "\xe3\xb0\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f"
        "\xb9\x24\x27\xae\x41\xe4\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52\xb8\x55"
    );

    Sha256 hash;
    hash.update("a");
    hash.update("bc");
    EXPECT_EQ(hash.digest(), Sha256::compute("abc"));
}

TEST(BytecodeCacheTest, RoundTripPreservesBehavior) {
    auto chunk = compileScript(kSource);

    auto blob = BytecodeSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto restored = BytecodeSerializer::deserialize(blob.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    EXPECT_EQ(runChunk(std::make_shared<BytecodeChunk>(std::move(*restored))), "42");
}

TEST(BytecodeCacheTest, RoundTripIsStableAcrossCycles) {
    auto chunk = compileScript(kSource);

    auto first = BytecodeSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(first.has_value());

    auto restored = BytecodeSerializer::deserialize(first.value(), headerFor(kSource));
    ASSERT_TRUE(restored.has_value());

    auto second = BytecodeSerializer::serialize(*restored, headerFor(kSource));
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(first.value(), second.value());
}

TEST(BytecodeCacheTest, RoundTripWithImportHashes) {
    auto chunk = compileScript(kSource);

    const std::string libSource = "func base() -> int {\n    return 1;\n}";
    auto header = headerFor(kSource, { libSource });

    auto blob = BytecodeSerializer::serialize(*chunk, header);
    ASSERT_TRUE(blob.has_value());

    auto restored = BytecodeSerializer::deserialize(blob.value(), header);
    ASSERT_TRUE(restored.has_value());
}

TEST(BytecodeCacheTest, RejectsChangedSourceHash) {
    auto chunk = compileScript(kSource);

    auto blob = BytecodeSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    auto stale = headerFor(kSource + "\n");
    EXPECT_FALSE(BytecodeSerializer::deserialize(blob.value(), stale).has_value());
}

TEST(BytecodeCacheTest, RejectsChangedImportHash) {
    auto chunk = compileScript(kSource);

    const std::string libSource = "func base() -> int {\n    return 1;\n}";
    auto blob = BytecodeSerializer::serialize(*chunk, headerFor(kSource, { libSource }));
    ASSERT_TRUE(blob.has_value());

    auto stale = headerFor(kSource, { libSource + "\n" });
    EXPECT_FALSE(BytecodeSerializer::deserialize(blob.value(), stale).has_value());
}

TEST(BytecodeCacheTest, RejectsImportCountMismatch) {
    auto chunk = compileScript(kSource);

    const std::string libSource = "func base() -> int {\n    return 1;\n}";
    auto blob = BytecodeSerializer::serialize(*chunk, headerFor(kSource, { libSource }));
    ASSERT_TRUE(blob.has_value());

    auto stale = headerFor(kSource, { libSource, libSource });
    EXPECT_FALSE(BytecodeSerializer::deserialize(blob.value(), stale).has_value());
}

TEST(BytecodeCacheTest, RejectsCorruptedBlob) {
    auto chunk = compileScript(kSource);

    auto blob = BytecodeSerializer::serialize(*chunk, headerFor(kSource));
    ASSERT_TRUE(blob.has_value());

    const auto& header = headerFor(kSource);

    EXPECT_FALSE(BytecodeSerializer::deserialize("not a cache blob", header).has_value());

    auto truncated = blob.value().substr(0, blob.value().size() - 1);
    EXPECT_FALSE(BytecodeSerializer::deserialize(truncated, header).has_value());

    auto damaged = blob.value();
    damaged[damaged.size() / 2] = static_cast<char>(damaged[damaged.size() / 2] ^ 0x40);
    EXPECT_FALSE(BytecodeSerializer::deserialize(damaged, header).has_value());
}
