#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"

using namespace LOICollection::frontend;

namespace {
    /* The std library models an Fn trait as a single-method class; a closure
     * should be usable wherever one is expected. */
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

/* P4: a closure satisfies a single-method interface, so `f.call(v)` runs the
 * closure body instead of requiring an Fn1 subclass. */
TEST(ClosureTraitTest, ClosureSatisfiesSingleMethodInterface) {
    EXPECT_EQ(eval(kFn1Source + "\n apply(func (x) { return x + 1; }, 41)"), "42");
}

/* Captured variables must still be visible inside the closure. */
TEST(ClosureTraitTest, ClosureKeepsCaptures) {
    EXPECT_EQ(eval(kFn1Source + "\n let k = 10\n apply(func (x) { return x + k; }, 5)"), "15");
}

/* The interface may inherit `call` from a base class. */
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

/* An existing Fn1 subclass must keep working unchanged. */
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

/* An unannotated parameter is the common std style: `f.call(v)` must still
 * reach the closure. */
TEST(ClosureTraitTest, ClosureCallWithoutTypeAnnotation) {
    EXPECT_EQ(eval("func apply(f, v) { return f.call(v); }\n"
                   " apply(func (x) { return x + 1; }, 41)"), "42");
}

/* A plain closure used as a value is unaffected. */
TEST(ClosureTraitTest, ClosureAsValueUnchanged) {
    EXPECT_EQ(eval("let f = func (a) { return a + 1; }; f(1)"), "2");
}

/* A type without a `call` method is not a single-method interface, so a
 * closure does not satisfy it. */
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
