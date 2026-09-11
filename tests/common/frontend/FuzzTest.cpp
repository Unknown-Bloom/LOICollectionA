#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <cstdlib>
#include <string>
#include <vector>

#include "LOICollectionA/frontend/ComponentExpander.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/MirSerializer.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"

namespace LOICollection::frontend {
    namespace {
        class Rng {
        public:
            explicit Rng(std::uint64_t seed) : state(seed) {}

            std::uint64_t next() {
                this->state += 0x9E3779B97F4A7C15ull;

                std::uint64_t value = this->state;
                value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
                value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;

                return value ^ (value >> 31);
            }

            std::size_t below(std::size_t bound) {
                return bound == 0 ? 0 : static_cast<std::size_t>(this->next() % bound);
            }

        private:
            std::uint64_t state;
        };

        const std::vector<std::string>& corpus() {
            static const std::vector<std::string> seeds = {
                "let a = 1;",
                "func add(x: int, y: int) -> int { return x + y; }",
                "class Deck { let cards = []; func size() -> int { return this.cards.length(); } }",
                "trait Iterable { func length() -> int; func element(i: int); }",
                "impl Iterable for Deck { func length() -> int { return 0; } func element(i: int) {} }",
                "for (i in 0..10) { println(i); }",
                "let m = new CustomForm(); m.setTitle(\"t\");",
                "let s = \"a\" + \"b\"; let n = s.length();",
                "if (a > 1) { println(1); } : { println(2); }",
                "let v: optional<int> = 1; if (v.has_value) { println(v!); }",
            };

            return seeds;
        }

        std::string mutate(const std::string& input, Rng& rng) {
            std::string result = input;

            const std::size_t rounds = 1 + rng.below(4);
            for (std::size_t i = 0; i < rounds && !result.empty(); ++i) {
                switch (rng.below(5)) {
                    case 0:
                        result[rng.below(result.size())] = static_cast<char>(rng.next());
                        break;
                    case 1:
                        result.erase(rng.below(result.size()));
                        break;
                    case 2:
                        result.insert(rng.below(result.size()), 1, static_cast<char>(rng.next()));
                        break;
                    case 3:
                        result.resize(rng.below(result.size() + 1));
                        break;
                    default: {
                        const auto& seeds = corpus();
                        result.insert(rng.below(result.size() + 1), seeds[rng.below(seeds.size())]);
                        break;
                    }
                }
            }

            return result;
        }

        std::string tokenSoup(Rng& rng) {
            static const std::vector<std::string> tokens = {
                "let", "const", "func", "class", "trait", "impl", "for", "in", "if", "while",
                "return", "new", "this", "super", "static", "using", "import", "component",
                "(", ")", "{", "}", "[", "]", ":", ";", ",", ".", "=", "==", "!", "!=",
                "+", "-", "*", "/", "..", "->", "?", "??", "?.", "<", ">", "<=", ">=",
                "0", "1", "42", "3.14", "\"str\"", "true", "false", "None",
                "int", "float", "string", "bool", "void", "optional", "variant",
                "a", "b", "value", "Deck", "CustomForm",
            };

            std::string result;
            const std::size_t count = rng.below(48);

            for (std::size_t i = 0; i < count; ++i) {
                result += tokens[rng.below(tokens.size())];
                if (rng.below(4) == 0)
                    result += ' ';
            }

            return result;
        }

        void runFrontend(const std::string& source) {
            DiagnosticEngine diagnostics;
            Lexer lexer(source, diagnostics);
            Parser parser(lexer, diagnostics);

            auto ast = parser.parse();
            if (!ast || ast->getType() != ASTNode::Type::Program)
                return;

            auto& program = static_cast<ProgramNode&>(*ast);
            if (!ComponentExpander::expand(program, diagnostics) || diagnostics.hasErrors())
                return;

            SemanticAnalyzer analyzer(diagnostics);
            analyzer.analyze(program);
        }

        std::shared_ptr<ir::MirChunk> runCompile(const std::string& source, DiagnosticEngine& diagnostics) {
            Lexer lexer(source, diagnostics);
            Parser parser(lexer, diagnostics);

            auto ast = parser.parse();
            if (!ast || diagnostics.hasErrors())
                return nullptr;

            if (ast->getType() == ASTNode::Type::Program) {
                auto& program = static_cast<ProgramNode&>(*ast);
                if (!ComponentExpander::expand(program, diagnostics) || diagnostics.hasErrors())
                    return nullptr;

                SemanticAnalyzer analyzer(diagnostics);
                analyzer.analyze(program);
                if (diagnostics.hasErrors())
                    return nullptr;
            }

            ir::Compiler compiler(diagnostics);
            auto chunk = std::make_shared<ir::MirChunk>(compiler.compile(*ast));
            if (diagnostics.hasErrors())
                return nullptr;

            ir::Optimizer optimizer;
            optimizer.optimize(*chunk);

            return chunk;
        }

        ir::MirSerializer::Header fuzzHeader() {
            return ir::MirSerializer::Header{ "fuzz", std::string(32, 'A'), std::nullopt, {} };
        }

        void runSerialization(const std::string& source, Rng& rng) {
            DiagnosticEngine diagnostics;
            const auto chunk = runCompile(source, diagnostics);
            if (!chunk)
                return;

            const auto header = fuzzHeader();
            std::string checksum;

            const auto blob = ir::MirSerializer::serialize(*chunk, header, &checksum);
            ASSERT_TRUE(blob.has_value());

            const auto restored = ir::MirSerializer::deserialize(*blob, header);
            ASSERT_TRUE(restored.has_value());

            std::string mutated = *blob;
            for (std::size_t i = 0; i < 4; ++i)
                if (!mutated.empty())
                    mutated[rng.below(mutated.size())] = static_cast<char>(rng.next());

            if (mutated != *blob)
                EXPECT_FALSE(ir::MirSerializer::deserialize(mutated, header).has_value());

            for (std::size_t i = 0; i < 8; ++i) {
                std::string garbage(i + 1, static_cast<char>(rng.next()));
                EXPECT_FALSE(ir::MirSerializer::deserialize(garbage, header).has_value());
            }
        }

        std::size_t fuzzRounds() {
            if (const auto* value = std::getenv("LOICOLLECTION_A_FUZZ_ROUNDS")) {
                const long parsed = std::atol(value);
                if (parsed > 0)
                    return static_cast<std::size_t>(parsed);
            }

            return 2000;
        }

        constexpr auto kPerInputLimit = std::chrono::milliseconds(250);
    }

    TEST(FuzzTest, MutatedSourcesStayBounded) {
        Rng rng(0x5DEECE66Dull);
        const auto& seeds = corpus();
        const std::size_t rounds = fuzzRounds();

        for (std::size_t i = 0; i < rounds; ++i) {
            const std::string source = mutate(seeds[rng.below(seeds.size())], rng);

            const auto startedAt = std::chrono::steady_clock::now();
            runFrontend(source);
            const auto elapsed = std::chrono::steady_clock::now() - startedAt;

            ASSERT_LE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                kPerInputLimit.count())
                << "frontend exceeded the per-input budget on: " << source;
        }
    }

    TEST(FuzzTest, TokenSoupStaysBounded) {
        Rng rng(0x2545F4914F6CDD1Dull);
        const std::size_t rounds = fuzzRounds();

        for (std::size_t i = 0; i < rounds; ++i) {
            const std::string source = tokenSoup(rng);

            const auto startedAt = std::chrono::steady_clock::now();
            runFrontend(source);
            const auto elapsed = std::chrono::steady_clock::now() - startedAt;

            ASSERT_LE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                kPerInputLimit.count())
                << "frontend exceeded the per-input budget on: " << source;
        }
    }

    TEST(FuzzTest, SerializerRejectsCorruptedPackages) {
        Rng rng(0x9E3779B97F4A7C15ull);
        const auto& seeds = corpus();

        for (const auto& seed : seeds)
            runSerialization(seed, rng);
    }
}
