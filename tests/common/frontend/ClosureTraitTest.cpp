#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"

using namespace LOICollection::frontend;

namespace {
    const std::string kFn1Source = R"(
        class Fn1 {
            func call(x) {
                return x;
            }
        }

        func apply(f: Fn1, v) {
            return f.call(v);
        }
    )";
}

TEST(ClosureTraitTest, ClosureSatisfiesSingleMethodInterface) {
    EXPECT_EQ(eval(kFn1Source + "\n apply(func (x) { return x + 1; }, 41)"), "42");
}

TEST(ClosureTraitTest, ClosureKeepsCaptures) {
    EXPECT_EQ(eval(kFn1Source + "\n let k = 10\n apply(func (x) { return x + k; }, 5)"), "15");
}

TEST(ClosureTraitTest, InheritedCallMethodQualifies) {
    EXPECT_EQ(eval(R"(
        class Base {
            func call(x) {
                return x;
            }
        }

        class Derived extends Base {
        }

        func apply(f: Base, v) {
            return f.call(v);
        }

        apply(func (x) { return x * 2; }, 21)
    )"), "42");
}

TEST(ClosureTraitTest, SubclassStillWorks) {
    EXPECT_EQ(eval(kFn1Source + R"(
        class Doubler extends Fn1 {
            func call(x) {
                return x * 2;
            }
        }

        apply(new Doubler(), 21)
    )"), "42");
}

TEST(ClosureTraitTest, ClosureCallWithoutTypeAnnotation) {
    EXPECT_EQ(eval("func apply(f, v) { return f.call(v); }\n"
                   " apply(func (x) { return x + 1; }, 41)"), "42");
}

TEST(ClosureTraitTest, ClosureAsValueUnchanged) {
    EXPECT_EQ(eval("let f = func (a) { return a + 1; }; f(1)"), "2");
}

TEST(ClosureTraitTest, TypeWithoutCallRejectsClosure) {
    DiagnosticEngine diagnostics;
    auto mir = compile(R"(
        class NotCallable {
            func other(x) {
                return x;
            }
        }

        func apply(f: NotCallable, v) {
            return f.other(v);
        }

        apply(func (x) { return x; }, 1)
    )", diagnostics);

    EXPECT_EQ(mir, nullptr);
    EXPECT_TRUE(diagnostics.hasErrors());
}
