#include <gtest/gtest.h>

#include <initializer_list>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

using namespace LOICollection::frontend::ir;
using namespace LOICollection::frontend::ir::opt;

namespace op_traits_test_support {
    bool isOneOf(Op op, std::initializer_list<Op> members) {
        for (const Op member : members)
            if (op == member)
                return true;

        return false;
    }
}

TEST(OpTraitsTest, IsJumpCoversEveryConditionalAndUnconditionalForm) {
    for (int value = 0; value < static_cast<int>(Op::COUNT); ++value) {
        const auto op = static_cast<Op>(value);

        EXPECT_EQ(isJump(op), op_traits_test_support::isOneOf(op, {
            Op::JMP, Op::JMP_IF_FALSE, Op::JMP_IF_TRUE, Op::JMP_IF_CMP_FALSE,
            Op::JMP_IF_TAG_NE
        })) << "op " << value;
    }
}

TEST(OpTraitsTest, IsTerminatorStopsFlowWithoutBeingAJump) {
    for (int value = 0; value < static_cast<int>(Op::COUNT); ++value) {
        const auto op = static_cast<Op>(value);

        EXPECT_EQ(isTerminator(op), op_traits_test_support::isOneOf(op, {
            Op::JMP, Op::RETURN, Op::HALT, Op::PANIC
        })) << "op " << value;
    }

    EXPECT_TRUE(isTerminator(Op::JMP));
    EXPECT_FALSE(isTerminator(Op::JMP_IF_FALSE));
    EXPECT_FALSE(isTerminator(Op::JMP_IF_TRUE));
    EXPECT_TRUE(isJump(Op::JMP_IF_FALSE));
    EXPECT_TRUE(isJump(Op::JMP_IF_TRUE));
    EXPECT_TRUE(isJump(Op::JMP_IF_CMP_FALSE));
}

TEST(OpTraitsTest, IsStoreOpOnlyCoversVariableSlots) {
    for (int value = 0; value < static_cast<int>(Op::COUNT); ++value) {
        const auto op = static_cast<Op>(value);

        EXPECT_EQ(isStoreOp(op), op_traits_test_support::isOneOf(op, {
            Op::STORE_VAR, Op::STORE_SLOT
        })) << "op " << value;
    }

    EXPECT_FALSE(isStoreOp(Op::STORE_FIELD));
    EXPECT_FALSE(isStoreOp(Op::STORE_FIELD_SLOT));
    EXPECT_FALSE(isStoreOp(Op::STORE_INDEX));
}

TEST(OpTraitsTest, IsPureBinaryExcludesFieldAndIndexAccess) {
    for (int value = 0; value < static_cast<int>(Op::COUNT); ++value) {
        const auto op = static_cast<Op>(value);

        EXPECT_EQ(isPureBinary(op), op_traits_test_support::isOneOf(op, {
            Op::ADD, Op::SUB, Op::MUL, Op::DIV, Op::MOD, Op::POW,
            Op::CMP_EQ, Op::CMP_NE, Op::CMP_GT, Op::CMP_LT, Op::CMP_GE, Op::CMP_LE,
            Op::LOGIC_AND, Op::LOGIC_OR
        })) << "op " << value;
    }

    EXPECT_FALSE(isPureBinary(Op::LOAD_INDEX));
    EXPECT_FALSE(isPureBinary(Op::LOAD_FIELD));
    EXPECT_FALSE(isPureBinary(Op::LOAD_LEN));
}

TEST(OpTraitsTest, IsPureUnaryCoversNegationAndReflection) {
    for (int value = 0; value < static_cast<int>(Op::COUNT); ++value) {
        const auto op = static_cast<Op>(value);

        EXPECT_EQ(isPureUnary(op), op_traits_test_support::isOneOf(op, {
            Op::NEG, Op::NOT, Op::INSTANCEOF, Op::LOAD_LEN,
            Op::TAG_OF, Op::LOAD_ENUM_PAYLOAD
        })) << "op " << value;
    }

    EXPECT_FALSE(isPureUnary(Op::LOAD_CONST));
    EXPECT_FALSE(isPureUnary(Op::LOAD_VAR));
}

TEST(OpTraitsTest, CanWriteVariablesCoversCallsAndAllocation) {
    for (int value = 0; value < static_cast<int>(Op::COUNT); ++value) {
        const auto op = static_cast<Op>(value);

        EXPECT_EQ(canWriteVariables(op), op_traits_test_support::isOneOf(op, {
            Op::CALL, Op::CALL_MACRO,
            Op::CALL_METHOD, Op::CALL_METHOD_VIRTUAL, Op::CALL_METHOD_BY_NAME,
            Op::CALL_FUNC, Op::CALL_NATIVE_METHOD, Op::CALL_LAMBDA, Op::CALL_SUPER_CTOR,
            Op::NEW, Op::STORE_FIELD
        })) << "op " << value;
    }

    EXPECT_TRUE(canWriteVariables(Op::STORE_FIELD));
    EXPECT_FALSE(canWriteVariables(Op::STORE_VAR));
    EXPECT_FALSE(canWriteVariables(Op::LOAD_VAR));
}

TEST(OpTraitsTest, PureOpsNeverWriteVariables) {
    for (int value = 0; value < static_cast<int>(Op::COUNT); ++value) {
        const auto op = static_cast<Op>(value);

        if (!isPureBinary(op) && !isPureUnary(op))
            continue;

        EXPECT_FALSE(canWriteVariables(op)) << "op " << value;
    }
}
