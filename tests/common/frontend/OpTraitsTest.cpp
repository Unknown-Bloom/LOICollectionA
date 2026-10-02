#include <gtest/gtest.h>

#include <initializer_list>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

using namespace LOICollection::frontend::ir;
using namespace LOICollection::frontend::ir::opt;

namespace op_traits_test_support {
    bool isOneOf(MirOp op, std::initializer_list<MirOp> members) {
        for (const MirOp member : members)
            if (op == member)
                return true;

        return false;
    }
}

TEST(OpTraitsTest, IsJumpCoversEveryConditionalAndUnconditionalForm) {
    for (int value = 0; value < static_cast<int>(MirOp::COUNT); ++value) {
        const auto op = static_cast<MirOp>(value);

        EXPECT_EQ(isJump(op), op_traits_test_support::isOneOf(op, {
            MirOp::JMP, MirOp::JMP_IF_FALSE, MirOp::JMP_IF_TRUE
        })) << "op " << value;
    }
}

TEST(OpTraitsTest, IsTerminatorStopsFlowWithoutBeingAJump) {
    for (int value = 0; value < static_cast<int>(MirOp::COUNT); ++value) {
        const auto op = static_cast<MirOp>(value);

        EXPECT_EQ(isTerminator(op), op_traits_test_support::isOneOf(op, {
            MirOp::JMP, MirOp::RETURN, MirOp::HALT
        })) << "op " << value;
    }

    EXPECT_TRUE(isTerminator(MirOp::JMP));
    EXPECT_FALSE(isTerminator(MirOp::JMP_IF_FALSE));
    EXPECT_FALSE(isTerminator(MirOp::JMP_IF_TRUE));
    EXPECT_TRUE(isJump(MirOp::JMP_IF_FALSE));
    EXPECT_TRUE(isJump(MirOp::JMP_IF_TRUE));
}

TEST(OpTraitsTest, IsStoreOpOnlyCoversVariableSlots) {
    for (int value = 0; value < static_cast<int>(MirOp::COUNT); ++value) {
        const auto op = static_cast<MirOp>(value);

        EXPECT_EQ(isStoreOp(op), op_traits_test_support::isOneOf(op, {
            MirOp::STORE_VAR, MirOp::STORE_SLOT
        })) << "op " << value;
    }

    EXPECT_FALSE(isStoreOp(MirOp::STORE_FIELD));
    EXPECT_FALSE(isStoreOp(MirOp::STORE_FIELD_SLOT));
    EXPECT_FALSE(isStoreOp(MirOp::STORE_INDEX));
}

TEST(OpTraitsTest, IsPureBinaryExcludesFieldAndIndexAccess) {
    for (int value = 0; value < static_cast<int>(MirOp::COUNT); ++value) {
        const auto op = static_cast<MirOp>(value);

        EXPECT_EQ(isPureBinary(op), op_traits_test_support::isOneOf(op, {
            MirOp::ADD, MirOp::SUB, MirOp::MUL, MirOp::DIV, MirOp::MOD, MirOp::POW,
            MirOp::CMP_EQ, MirOp::CMP_NE, MirOp::CMP_GT, MirOp::CMP_LT, MirOp::CMP_GE, MirOp::CMP_LE,
            MirOp::LOGIC_AND, MirOp::LOGIC_OR
        })) << "op " << value;
    }

    EXPECT_FALSE(isPureBinary(MirOp::LOAD_INDEX));
    EXPECT_FALSE(isPureBinary(MirOp::LOAD_FIELD));
    EXPECT_FALSE(isPureBinary(MirOp::LOAD_LEN));
}

TEST(OpTraitsTest, IsPureUnaryCoversNegationAndReflection) {
    for (int value = 0; value < static_cast<int>(MirOp::COUNT); ++value) {
        const auto op = static_cast<MirOp>(value);

        EXPECT_EQ(isPureUnary(op), op_traits_test_support::isOneOf(op, {
            MirOp::NEG, MirOp::NOT, MirOp::INSTANCEOF, MirOp::LOAD_LEN
        })) << "op " << value;
    }

    EXPECT_FALSE(isPureUnary(MirOp::LOAD_CONST));
    EXPECT_FALSE(isPureUnary(MirOp::LOAD_VAR));
}

TEST(OpTraitsTest, CanWriteVariablesCoversCallsAndAllocation) {
    for (int value = 0; value < static_cast<int>(MirOp::COUNT); ++value) {
        const auto op = static_cast<MirOp>(value);

        EXPECT_EQ(canWriteVariables(op), op_traits_test_support::isOneOf(op, {
            MirOp::CALL, MirOp::CALL_MACRO,
            MirOp::CALL_METHOD, MirOp::CALL_METHOD_VIRTUAL, MirOp::CALL_METHOD_BY_NAME,
            MirOp::CALL_FUNC, MirOp::CALL_NATIVE_METHOD, MirOp::CALL_LAMBDA, MirOp::CALL_SUPER_CTOR,
            MirOp::NEW, MirOp::STORE_FIELD
        })) << "op " << value;
    }

    EXPECT_TRUE(canWriteVariables(MirOp::STORE_FIELD));
    EXPECT_FALSE(canWriteVariables(MirOp::STORE_VAR));
    EXPECT_FALSE(canWriteVariables(MirOp::LOAD_VAR));
}

TEST(OpTraitsTest, PureOpsNeverWriteVariables) {
    for (int value = 0; value < static_cast<int>(MirOp::COUNT); ++value) {
        const auto op = static_cast<MirOp>(value);

        if (!isPureBinary(op) && !isPureUnary(op))
            continue;

        EXPECT_FALSE(canWriteVariables(op)) << "op " << value;
    }
}
