#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/frontend/CommonTest.h"

#include "LOICollectionA/frontend/Callback.h"

using namespace LOICollection::frontend;

namespace {
    struct IterableBagHandle : NativeHandle {
        std::vector<TypedValue> items;
    };

    ll::Expected<ObjectRef> makeIterableBag(const CallbackTypeValues& args) {
        auto handle = std::make_shared<IterableBagHandle>();
        handle->items = std::get<ArrayRef>(args[0])->elements;

        auto obj = std::make_shared<Object>();
        obj->className = "IterableBag";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    ll::Expected<TypedValue> iterableBagLength(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<int>(static_cast<IterableBagHandle*>(self->native.get())->items.size());
    }

    ll::Expected<TypedValue> iterableBagElement(const ObjectRef& self, const CallbackTypeValues& args) {
        auto& items = static_cast<IterableBagHandle*>(self->native.get())->items;

        int index = std::get<int>(args[0]);
        if (index < 0 || static_cast<std::size_t>(index) >= items.size())
            return ll::makeStringError("IterableBag element index out of range");

        return items[static_cast<std::size_t>(index)];
    }

    void registerIterableBag() {
        static const bool registered = [] {
            ClassCall& classes = ClassCall::getInstance();

            classes.registerClass("IterableBag", {});
            classes.registerConstructor("IterableBag", makeIterableBag, { ParamType::ARRAY });
            classes.registerMethod("IterableBag", "length", iterableBagLength, {});
            classes.registerMethod("IterableBag", "element", iterableBagElement, { ParamType::INT });

            return true;
        }();

        (void)registered;
    }
}

TEST(ForInTest, IteratesArray) {
    EXPECT_EQ(eval("let total = 0; for (x in [1, 2, 3]) [ total += x; ]; total"), "6");
    EXPECT_EQ(eval("let total = \"\"; for (x in [\"a\", \"b\"]) [ total += x; ]; total"), "ab");
}

TEST(ForInTest, EmptyArray) {
    EXPECT_EQ(eval("let total = 0; for (x in []) [ total += 1; ]; total"), "0");
}

TEST(ForInTest, WithIndex) {
    EXPECT_EQ(eval("let sum = 0; for (i, x in [10, 20, 30]) [ sum += i; ]; sum"), "3");
    EXPECT_EQ(eval("let pairs = \"\"; for (i, x in [\"a\", \"b\"]) [ pairs += i; ]; pairs"), "01");
}

TEST(ForInTest, IndexPairsWithValue) {
    EXPECT_EQ(eval("let out = \"\"; for (i, x in [\"a\", \"b\", \"c\"]) [ out += x; ]; out"), "abc");
}

TEST(ForInTest, Range) {
    EXPECT_EQ(eval("let total = 0; for (i in 0..5) [ total += i; ]; total"), "10");
    EXPECT_EQ(eval("let total = 0; for (i in 2..2) [ total += i; ]; total"), "0");
}

TEST(ForInTest, DescendingRange) {
    EXPECT_EQ(eval("let total = 0; for (i in 3..0) [ total += i; ]; total"), "6");
    EXPECT_EQ(eval("let out = \"\"; for (i in 2..0) [ out += i; ]; out"), "21");
}

TEST(ForInTest, RangeWithExpressions) {
    EXPECT_EQ(eval("let n = 3; let total = 0; for (i in 0..n) [ total += i; ]; total"), "3");
}

TEST(ForInTest, ContinueSkipsBodyButNotIteration) {
    EXPECT_EQ(eval(
        "let total = 0; "
        "for (x in [1, 2, 3, 4]) [ "
        "    if (x == 2) [ continue; ]; "
        "    total += x; "
        "]; total"),
        "8");
}

TEST(ForInTest, BreakExitsLoop) {
    EXPECT_EQ(eval(
        "let total = 0; "
        "for (x in [1, 2, 3, 4]) [ "
        "    if (x == 3) [ break; ]; "
        "    total += x; "
        "]; total"),
        "3");
}

TEST(ForInTest, SeesAppendedElements) {
    EXPECT_EQ(eval(
        "let arr = [1, 2, 3]; "
        "let count = 0; "
        "for (x in arr) [ count += 1; if (count < 5) [ arr.push(count); ] ]; "
        "count"),
        "7");
}

TEST(ForInTest, Nested) {
    EXPECT_EQ(eval(
        "let pairs = \"\"; "
        "for (i in 0..2) [ for (j in 0..2) [ pairs += i; pairs += j; ] ]; "
        "pairs"),
        "00011011");
}

TEST(ForInTest, IteratesString) {
    EXPECT_EQ(eval("let out = \"\"; for (c in \"abc\") [ out += c; ]; out"), "abc");
    EXPECT_EQ(eval("let out = \"\"; for (i, c in \"abc\") [ out += i; ]; out"), "012");
    EXPECT_EQ(eval("let out = \"\"; for (c in \"\") [ out += c; ]; out"), "");
}

TEST(ForInTest, IteratesStringByCodepoint) {
    EXPECT_EQ(eval("let out = \"\"; for (c in \"你好\") [ out += c; out += \"-\"; ]; out"), "你-好-");
    EXPECT_EQ(eval("let n = 0; for (c in \"a你b\") [ n += 1; ]; n"), "3");
}

TEST(ForInTest, IteratesMapKeys) {
    EXPECT_EQ(eval(
        "let m = new Map(); m.set(\"a\", 1); m.set(\"b\", 2); "
        "let out = \"\"; for (k in m) [ out += k; ]; out"),
        "ab");
    EXPECT_EQ(eval(
        "let m = new Map(); m.set(\"a\", 1); m.set(\"b\", 2); "
        "let out = \"\"; for (i, k in m) [ out += i; out += k; ]; out"),
        "0a1b");
    EXPECT_EQ(eval(
        "let m = new Map(); m.set(7, \"x\"); "
        "let out = \"\"; for (k in m) [ out += k; out += m.get(k); ]; out"),
        "7x");
}

TEST(ForInTest, SeesMapEntriesAddedDuringIteration) {
    EXPECT_EQ(eval(
        "let m = new Map(); m.set(\"a\", 1); "
        "let n = 0; "
        "for (k in m) [ n += 1; if (n == 1) [ m.set(\"b\", 2); ] ]; n"),
        "2");
}

TEST(ForInTest, StringElementCarriesStringType) {
    EXPECT_THROW(eval("for (c in \"abc\") [ let n: int = c; ];"), std::runtime_error);
    EXPECT_EQ(eval("let out = \"\"; for (c in \"abc\") [ out += c.length(); ]; out"), "111");
}

TEST(ForInTest, IteratesClassProvidingLengthAndElement) {
    registerIterableBag();

    EXPECT_EQ(eval(
        "let bag = new IterableBag([1, 2, 3]); "
        "let out = \"\"; for (v in bag) [ out += v; ]; out"),
        "123");
    EXPECT_EQ(eval(
        "let bag = new IterableBag([\"a\", \"b\"]); "
        "let out = \"\"; for (i, v in bag) [ out += i; out += v; ]; out"),
        "0a1b");
}

TEST(ForInTest, RejectsClassWithoutTheConvention) {
    EXPECT_THROW(eval("let v = new CtxValue(0); for (x in v) [ x; ];"), std::runtime_error);
}

TEST(ForInTest, NonIterable) {
    EXPECT_THROW(eval("for (x in 5) [ x; ]"), std::runtime_error);
    EXPECT_THROW(eval("for (x in true) [ x; ]"), std::runtime_error);
}

TEST(ForInTest, LambdaCapturesCurrentIterationValue) {
    // A lambda created inside the loop must capture the value of its own
    // iteration, not a reference that later follows the loop variable.
    EXPECT_EQ(eval(
        "let fns = []; "
        "for (item in [10, 20, 30]) [ "
        "    fns.push(func () -> int { return item; }); "
        "]; "
        "let s = \"\"; for (f in fns) [ s += f(); ]; s"),
        "102030");
    EXPECT_EQ(eval(
        "let fns = []; "
        "for (i in 1..3) [ fns.push(func () -> int { return i * 10; }); ]; "
        "let s = \"\"; for (f in fns) [ s += f(); ]; s"),
        "1020");
    EXPECT_EQ(eval(
        "let fns = []; "
        "for (i, item in [\"a\", \"b\"]) [ "
        "    fns.push(func () -> int { return i; }); "
        "]; "
        "let last = fns[1]; last()"),
        "1");
}
