#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "LOICollectionA/LOICollectionA.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ScriptLoader.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"
#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"

using namespace LOICollection::frontend;

/* Runs entry "/main.lcui" against an in-memory file map that already
 * contains the real std modules (assets/common/std/*.lcui) under "/".
 * Std modules import each other by file-relative paths, which resolve
 * to "/iter.lcui" etc. — this exercises the ScriptLoader resolution the
 * shipped assets rely on. */

namespace {
    std::string evalStd(const std::string& program) {
        std::map<std::string, std::string> files = {
            {"/main.lcui", program},
        };

        /* Std modules ship inside the plugin directory, exactly like
         * `gui/permission.json`: resolve them against the mod dir first and
         * only fall back to workspace-relative paths used for local runs. */
        static const std::string stdDir = [] {
            std::vector<std::filesystem::path> candidates;

            const std::filesystem::path modDir =
                LOICollection::A::getInstance().getSelf().getModDir();
            if (!modDir.empty()) {
                candidates.push_back(modDir / "std");
                candidates.push_back(modDir / "assets" / "common" / "std");
            }
#ifdef LOICOLLECTION_A_STD_DIR
            candidates.push_back(std::filesystem::path(LOICOLLECTION_A_STD_DIR));
#endif
            for (const char* cand : {
                "assets/common/std",
                "../assets/common/std",
                "../../assets/common/std",
                "../../../assets/common/std",
                "../../../repo/assets/common/std",
            }) {
                candidates.push_back(std::filesystem::path(cand));
            }

            for (const auto& cand : candidates) {
                if (std::filesystem::exists(cand / "iter.lcui"))
                    return cand.string();
            }

            throw std::runtime_error("cannot locate assets/common/std (define LOICOLLECTION_A_STD_DIR)");
        }();

        for (const char* name : { "iter", "array", "string", "math", "result", "map", "json", "random", "time", "set", "vec", "num" }) {
            std::ifstream file(std::filesystem::path(stdDir) / (std::string(name) + ".lcui"));
            if (!file)
                throw std::runtime_error("cannot open std module: " + std::string(name));

            std::stringstream ss;
            ss << file.rdbuf();
            files["/" + std::string(name) + ".lcui"] = ss.str();
        }

        ScriptLoader::FileReader reader = [&files](const std::string& path) -> std::optional<std::string> {
            auto it = files.find(path);
            if (it == files.end())
                return std::nullopt;
            return it->second;
        };

        DiagnosticEngine diagnostics;
        auto loaded = ScriptLoader::load("/main.lcui", "/", reader, diagnostics);
        if (!loaded)
            throw std::runtime_error(diagnostics.getErrorMessage());

        SemanticAnalyzer analyzer(diagnostics);
        analyzer.analyze(*loaded->program);
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        ir::Compiler compiler(diagnostics);
        auto mir = std::make_shared<ir::MirChunk>(compiler.compile(*loaded->program));
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        ir::Optimizer optimizer;
        optimizer.optimize(*mir);

        ir::VM vm(diagnostics);
        auto result = vm.run(mir, {});
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        return ir::VM::valueToString(result);
    }
}

TEST(StdlibScriptTest, StringHelpers) {
    EXPECT_EQ(evalStd("import \"/string.lcui\"; upper(\"aBc\")"), "ABC");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; lower(\"XyZ\")"), "xyz");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; upper(\"中文abc\")"), "中文ABC");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; substr(\"hello\", 1, 3)"), "ell");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; substr(\"hello\", 10, 2)"), "");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; trim(\"  hi \")"), "hi");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; replace(\"abcabc\", \"a\", \"x\")"), "xbcxbc");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; replace(\"abc\", \"\", \"x\")"), "abc");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; capitalize(\"hello world\")"), "Hello world");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; reverse(\"abc\")"), "cba");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; padLeft(\"7\", 3, \"0\")"), "007");
}

TEST(StdlibScriptTest, MathHelpers) {
    EXPECT_EQ(evalStd("import \"/math.lcui\"; abs(-10)"), "10");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; abs(-3.5)"), "3.5");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; min(5, 3)"), "3");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; max(2.5, 1.5)"), "2.5");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; clamp(9, 0, 5)"), "5");
}

TEST(StdlibScriptTest, IteratorCombinatorsChain) {
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\";"
        "class Square extends Fn1 { func call(x) { return x * x; } }"
        "class Big extends Fn1 { func call(x) { return x > 4; } }"
        "range(1, 10).map(new Square()).filter(new Big()).collect().join(\",\")"),
        "9,16,25,36,49,64,81");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 10).skip(7).collect().join(\",\")"), "8,9");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 10).take(3).collect().join(\",\")"), "1,2,3");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 5).sum()"), "10");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 9).stepBy(3).collect().join(\",\")"), "1,4,7");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 5).chain(range(10, 12)).collect().join(\",\")"),
        "1,2,3,4,10,11");
}

TEST(StdlibScriptTest, IteratorSearchAndFold) {
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; class Big extends Fn1 { func call(x) { return x > 4; } }"
        "range(1, 10).find(new Big())"), "5");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; class Big extends Fn1 { func call(x) { return x > 4; } }"
        "range(1, 10).position(new Big())"), "4");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 5).max()"), "4");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 5).min()"), "1");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; repeat(\"x\").take(3).collect().join(\"\")"), "xxx");
}

TEST(StdlibScriptTest, ArrayHelpersPullFn1ThroughSelectiveImport) {
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class Twice extends Fn1 { func call(x) { return x * 2; } }"
        "arrMap([1, 2, 3], new Twice()).join(\",\")"), "2,4,6");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; arrSum([1, 2, 3])"), "6");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; arrDedup([1, 2, 1, 3, 2]).join(\",\")"), "1,2,3");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; arrLast([]) == None"), "true");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; arrZip([1, 2], [\"a\", \"b\"])[1][1]"), "b");
}

TEST(StdlibScriptTest, ResultType) {
    EXPECT_EQ(evalStd(
        "import \"/result.lcui\"; Result::Ok(42).unwrap()"), "42");
    EXPECT_EQ(evalStd(
        "import \"/result.lcui\"; Result::Err(\"boom\").unwrapOr(-1)"), "-1");
    EXPECT_EQ(evalStd(
        "import \"/result.lcui\"; Result::Err(\"boom\").errOr(\"none\")"), "boom");
    EXPECT_EQ(evalStd(
        "import \"/result.lcui\";"
        "class Inc extends Fn1 { func call(x) { return x + 1; } }"
        "Result::Ok(1).map(new Inc()).unwrap()"), "2");
    EXPECT_ANY_THROW(evalStd(
        "import \"/result.lcui\"; Result::Err(\"boom\").unwrap()"));
}

TEST(StdlibScriptTest, MathRoundingAndNumberTheory) {
    EXPECT_EQ(evalStd("import \"/math.lcui\"; trunc(3.7)"), "3");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; trunc(-2.5)"), "-2");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; floor(-2.5)"), "-3");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; ceil(3.2)"), "4");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; round(2.5)"), "3");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; round(-2.5)"), "-3");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; roundTo(3.14159, 2)"), "3.14");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; mod(-7, 3)"), "2");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; gcd(12, 18)"), "6");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; lcm(4, 6)"), "12");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; factorial(5)"), "120");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; fib(10)"), "55");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; isPrime(17)"), "true");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; isPrime(9)"), "false");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; sign(-3)"), "-1");
}

TEST(StdlibScriptTest, MathAggregates) {
    EXPECT_EQ(evalStd("import \"/math.lcui\"; hypot(3, 4)"), "5");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; log10(100)"), "2");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; mapRange(5, 0, 10, 0, 100)"), "50");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; clamp01(1.5)"), "1");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; sum([1, 2, 3])"), "6");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; mean([1, 2, 3, 4])"), "2.5");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; minOf([3, 1, 2])"), "1");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; maxOf([3, 1, 2])"), "3");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; median([3, 1, 2])"), "2");
}

TEST(StdlibScriptTest, StringSearchAndSplit) {
    EXPECT_EQ(evalStd("import \"/string.lcui\"; trimStart(\"  hi  \")"), "hi  ");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; trimEnd(\"  hi  \")"), "  hi");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; startsWith(\"hello\", \"he\")"), "true");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; endsWith(\"hello\", \"lo\")"), "true");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; indexOf(\"hello\", \"l\")"), "2");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; lastIndexOf(\"hello\", \"l\")"), "3");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; count(\"ababab\", \"ab\")"), "3");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; words(\"  a b   c \").join(\"|\")"), "a|b|c");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; lines(\"a\\nb\\r\\nc\").join(\"|\")"), "a|b|c");
}

TEST(StdlibScriptTest, StringTransform) {
    EXPECT_EQ(evalStd("import \"/string.lcui\"; isBlank(\"   \")"), "true");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; isUpper(\"A\")"), "true");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; isDigit(\"7\")"), "true");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; swapCase(\"aBcD\")"), "AbCd");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; titleCase(\"hello world\")"), "Hello World");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; truncate(\"abcdefgh\", 5, \"...\")"), "ab...");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; stripPrefix(\"TestFoo\", \"Test\")"), "Foo");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; stripSuffix(\"foo.txt\", \".txt\")"), "foo");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; commonPrefix(\"abcdef\", \"abcxyz\")"), "abc");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; commonSuffix(\"xxdef\", \"yydef\")"), "def");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; insert(\"abcd\", 2, \"XY\")"), "abXYcd");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; removeAt(\"abcdef\", 1, 2)"), "adef");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; equalsIgnoreCase(\"AbC\", \"aBc\")"), "true");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; snakeCase(\"helloWorld\")"), "hello_world");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; kebabCase(\"helloWorld\")"), "hello-world");
}

TEST(StdlibScriptTest, ArrayCombinators) {
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrFirst([5, 6])"), "5");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrFlatten([[1, 2], [3]]).join(\",\")"), "1,2,3");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrTake([1, 2, 3, 4], 2).join(\",\")"), "1,2");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrSkip([1, 2, 3, 4], 2).join(\",\")"), "3,4");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrConcat([1], [2, 3]).join(\",\")"), "1,2,3");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrMin([3, 1, 2])"), "1");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrMax([3, 1, 2])"), "3");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrMean([1, 2, 3, 4])"), "2.5");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrUnion([1, 2], [2, 3]).join(\",\")"), "1,2,3");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrIntersect([1, 2, 3], [2, 3, 4]).join(\",\")"), "2,3");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrDifference([1, 2, 3], [2]).join(\",\")"), "1,3");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrFill(3, \"x\").join(\",\")"), "x,x,x");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrRange(2, 5).join(\",\")"), "2,3,4");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrEqual([1, 2], [1, 2])"), "true");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrIsEmpty([])"), "true");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrIndexOfValue([7, 8, 9], 8)"), "1");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrRotate([1, 2, 3, 4], 1).join(\",\")"), "2,3,4,1");
    EXPECT_EQ(evalStd("import \"/array.lcui\"; arrWindow([1, 2, 3, 4], 2).length()"), "3");
}

TEST(StdlibScriptTest, ArrayWithFunctionObjects) {
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class IsEvenFn extends Fn1 { func call(x) { return x % 2 == 0; } }"
        "arrCount([1, 2, 3, 4], new IsEvenFn())"), "2");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class SumFn extends Fn2 { func call(a, b) { return a + b; } }"
        "arrReduce([1, 2, 3], new SumFn(), 10)"), "16");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class Twice extends Fn1 { func call(x) { return x * 2; } }"
        "arrSortBy([3, 1, 2], new Twice()).join(\",\")"), "1,2,3");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class IsEvenFn extends Fn1 { func call(x) { return x % 2 == 0; } }"
        "arrPartition([1, 2, 3, 4], new IsEvenFn()).length()"), "2");
}

TEST(StdlibScriptTest, MapAndSetHelpers) {
    EXPECT_EQ(evalStd(
        "import \"/map.lcui\";"
        "let m = mapNew(); mapSet(m, \"a\", 1); mapSet(m, \"b\", 2); mapGet(m, \"a\")"), "1");
    EXPECT_EQ(evalStd(
        "import \"/map.lcui\";"
        "let m = mapNew(); mapSet(m, \"a\", 1); mapGetOr(m, \"zz\", 99)"), "99");
    EXPECT_EQ(evalStd(
        "import \"/map.lcui\";"
        "let m = mapNew(); mapSet(m, \"a\", 1); mapHas(m, \"a\")"), "true");
    EXPECT_EQ(evalStd(
        "import \"/map.lcui\";"
        "let m = mapNew(); mapSet(m, \"a\", 1); mapSet(m, \"b\", 2); mapKeys(m).join(\",\")"), "a,b");
    EXPECT_EQ(evalStd(
        "import \"/map.lcui\";"
        "let m = mapNew(); mapSet(m, \"a\", 1); mapSet(m, \"b\", 2); mapValues(m).join(\",\")"), "1,2");
    EXPECT_EQ(evalStd(
        "import \"/map.lcui\";"
        "let m = mapNew(); mapSet(m, \"a\", 1); mapRemove(m, \"a\"); mapSize(m)"), "0");
    EXPECT_EQ(evalStd(
        "import \"/map.lcui\"; setHas(setFromArray([1, 2, 3]), 2)"), "true");
    EXPECT_EQ(evalStd(
        "import \"/map.lcui\"; setSize(setFromArray([1, 2, 3]))"), "3");
    EXPECT_EQ(evalStd(
        "import \"/map.lcui\"; setToArray(setUnion(setFromArray([1, 2]), setFromArray([2, 3]))).join(\",\")"),
        "1,2,3");
    EXPECT_EQ(evalStd(
        "import \"/map.lcui\"; setToArray(setIntersect(setFromArray([1, 2]), setFromArray([2, 3]))).join(\",\")"),
        "2");
    EXPECT_EQ(evalStd(
        "import \"/map.lcui\"; setToArray(setDifference(setFromArray([1, 2]), setFromArray([2, 3]))).join(\",\")"),
        "1");
}

TEST(StdlibScriptTest, JsonQuoting) {
    EXPECT_EQ(evalStd("import \"/json.lcui\"; jsonQuote(\"ab\")"), "\"ab\"");
    EXPECT_EQ(evalStd("import \"/json.lcui\"; jsonEscape(\"a\\tb\")"), "a\\tb");
}

TEST(StdlibScriptTest, ManualNextKeepsOptionalOnDynamicLet) {
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\";"
        "let it = range(1, 3);"
        "let a = it.next();"
        "let b = it.next();"
        "let c = it.next();"
        "let s = \"\";"
        "if (a) [ s = s + a; ];"
        "if (b) [ s = s + b; ];"
        "if (!c) [ s = s + \"end\"; ];"
        "s"), "12end");
}

TEST(StdlibScriptTest, NumberTheoryAndFormatting) {
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numClamp(15, 0, 10)"), "10");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numSign(-7)"), "-1");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIntDiv(17, 5)"), "3");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numGcd(48, 36)"), "12");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numLcm(4, 6)"), "12");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsPrime(17)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numPrimesUpTo(10).join(\",\")"), "2,3,5,7");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numFactorize(60).join(\",\")"), "2,2,3,5");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numFactorial(5)"), "120");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numFib(10)"), "55");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numDigits(1234).join(\",\")"), "4,3,2,1");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numSumDigits(123)"), "6");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numReverse(12345)"), "54321");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsPalindromeNum(121)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numToBin(255)"), "11111111");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numToHex(255)"), "ff");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numToRoman(1994)"), "MCMXCIV");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numLerp(0, 10, 0.5)"), "5");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numDivisors(12).join(\",\")"), "1,2,3,4,6,12");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numSumOfDivisors(12)"), "28");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsPerfect(6)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsPerfect(28)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsAbundant(12)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsDeficient(5)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numTotient(9)"), "6");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numDigitCount(12345)"), "5");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsArmstrong(153)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsArmstrong(123)"), "false");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numSumRange(1, 100)"), "5050");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numProductRange(1, 5)"), "120");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numManhattan(0, 0, 3, 4)"), "7");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsCoprime(8, 15)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsCoprime(8, 12)"), "false");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsPowerOfTwo(8)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsPowerOfTwo(6)"), "false");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numGreatestPowerOfTwoLe(10)"), "8");
}

TEST(StdlibScriptTest, VectorHelpers) {
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vAdd([1, 2], [3, 4]).join(\",\")"), "4,6");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vSub([3, 4], [1, 1]).join(\",\")"), "2,3");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vScale([1, 2], 3).join(\",\")"), "3,6");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vDot([1, 2], [3, 4])"), "11");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vLen([3, 4])"), "5");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vDist([0, 0], [3, 4])"), "5");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vCross([1, 0], [0, 1])"), "1");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vDistanceSq([0, 0], [3, 4])"), "25");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vManhattan([0, 0], [3, 4])"), "7");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vComponentSum([3,4])"), "7");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vIsZero([0, 0])"), "true");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vEq([1, 2], [1, 2])"), "true");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vEq([1, 2], [1, 3])"), "false");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vAbs([-3, 4]).join(\",\")"), "3,4");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vProject([4, 0], [2, 0]).join(\",\")"), "4,0");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vReflect([1, 1], [0, 1]).join(\",\")"), "1,-1");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vClampLen([3, 4], 2).join(\",\")"), "1.2,1.6");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vResize([3, 4], 2).join(\",\")"), "1.2,1.6");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vMidpoint([0, 0], [2, 4]).join(\",\")"), "1,2");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vFromAngle(0, 5).join(\",\")"), "5,0");
}

TEST(StdlibScriptTest, MigratedHigherOrderHelpers) {
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class Dbl extends Fn1 { func call(x) { return x * 2; } }"
        "let m = mapNew(); mapSet(m, \"a\", 1); mapSet(m, \"b\", 2);"
        "mapMapValues(m, new Dbl()).get(\"a\")"), "2");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class Ks extends Fn1 { func call(x) { return \"k_\" + x; } }"
        "let m = mapNew(); mapSet(m, \"a\", 1); mapSet(m, \"b\", 2);"
        "mapMapKeys(m, new Ks()).has(\"k_a\")"), "true");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class Gt1 extends Fn2 { func call(k, v) { return v > 1; } }"
        "let m = mapNew(); mapSet(m, \"a\", 1); mapSet(m, \"b\", 2);"
        "mapFilter(m, new Gt1()).has(\"b\")"), "true");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; import \"/map.lcui\";"
        "class Kf extends Fn1 { func call(x) { return x[0]; } }"
        "class Vf extends Fn1 { func call(x) { return x[1]; } }"
        "mapFromArray([[\"a\", 1], [\"b\", 2]], new Kf(), new Vf()).get(\"b\")"), "2");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class Even extends Fn1 { func call(x) { return x % 2 == 0; } }"
        "let m = mapNew(); mapSet(m, \"a\", 2); mapSet(m, \"b\", 3);"
        "mapKeysWhere(m, new Even()).length()"), "1");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class Inc extends Fn1 { func call(x) { return x + 1; } }"
        "let m = mapNew(); mapSet(m, \"a\", 1); mapUpdate(m, \"a\", new Inc()).get(\"a\")"), "2");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class Nop extends Fn2 { func call(k, v) { return 0; } }"
        "let m = mapNew(); mapSet(m, \"a\", 1); mapForEach(m, new Nop()); 7"), "7");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; import \"/map.lcui\";"
        "class Sq extends Fn1 { func call(x) { return x * x; } }"
        "setMap(setFromArray([1, 2, 3]), new Sq()).has(\"9\")"), "true");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; import \"/map.lcui\";"
        "class OddM extends Fn1 { func call(x) { return x % 2 == 1; } }"
        "setFilter(setFromArray([1, 2, 3, 4]), new OddM()).has(\"3\")"), "true");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; import \"/map.lcui\";"
        "class EvenM extends Fn1 { func call(x) { return x % 2 == 0; } }"
        "setCount(setFromArray([1, 2, 3,4]), new EvenM())"), "2");
}

TEST(StdlibScriptTest, NumberClassification) {
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numMin(3, 7)"), "3");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numMax(3, 7)"), "7");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsBetween(5, 1, 10)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsBetween(11, 1, 10)"), "false");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsSquare(25)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsSquare(26)"), "false");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsTriangular(6)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsTriangular(7)"), "false");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numTriangular(4)"), "10");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numFromDigits([4, 3, 2, 1])"), "1234");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numFromDigits(numDigits(1234))"), "1234");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsHarshad(18)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsHarshad(19)"), "false");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numCountDivisors(12)"), "6");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numGcd3(12, 18, 24)"), "6");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsEmirp(13)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsEmirp(11)"), "false");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsEmirp(23)"), "false");
}

TEST(StdlibScriptTest, VectorExtra) {
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vDotSelf([3, 4])"), "25");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vScaleAdd([1, 2], 2, [3, 4]).join(\",\")"), "7,10");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vNeg([1, 2]).join(\",\")"), "-1,-2");
    EXPECT_EQ(evalStd("import \"/vec.lcui\"; vPerp([1, 2]).join(\",\")"), "-2,1");
}

TEST(StdlibScriptTest, NumberTheoryExtra) {
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numSumOfDivisors(12)"), "28");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numSumOfProperDivisors(12)"), "16");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsHappy(19)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsHappy(4)"), "false");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsHappy(1)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numNextPrime(10)"), "11");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numNextPrime(13)"), "17");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numPrevPrime(10)"), "7");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numPrevPrime(2)"), "0");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsMersenne(3)"), "true");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numIsMersenne(5)"), "false");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numDigitalRoot(123)"), "6");
    EXPECT_EQ(evalStd("import \"/num.lcui\"; numDigitalRoot(999)"), "9");
}
