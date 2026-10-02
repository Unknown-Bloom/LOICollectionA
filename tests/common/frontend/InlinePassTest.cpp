#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"
#include "LOICollectionA/frontend/ir/opt/passes/InlinePass.h"

using namespace LOICollection::frontend::ir;
using namespace LOICollection::frontend::ir::opt;

namespace inline_pass_test_support {
    MirChunk makeCallerChunk() {
        MirChunk chunk;
        chunk.slotCount = 4;
        chunk.constants.push_back(1);

        MethodMeta meta;
        meta.name = "leaf";
        meta.argCount = 0;
        meta.classIndex = -1;
        meta.bodyIndex = 0;
        chunk.methods.push_back(meta);

        auto body = std::make_unique<MirChunk>();
        body->slotCount = 1;
        body->code = {
            { MirOp::LOAD_CONST, 0, 0 },
            { MirOp::RETURN, 0, -1, 0 },
        };
        chunk.methodBodies.push_back(std::move(body));

        chunk.code = {
            { MirOp::LOAD_CONST, 0, 0 },
            { MirOp::JMP_IF_FALSE, 2, -1, 0, -1 },
            { MirOp::CALL_METHOD, 0, 1, 0, -1, -1, 0 },
            { MirOp::LOAD_CONST, 0, 2 },
            { MirOp::HALT, 0 },
        };

        return chunk;
    }

    int indexOf(const MirChunk& chunk, MirOp op) {
        for (std::size_t i = 0; i < chunk.code.size(); ++i)
            if (chunk.code[i].op == op)
                return static_cast<int>(i);

        return -1;
    }

    int absoluteTarget(const MirChunk& chunk, int at) {
        return at + 1 + chunk.code[at].operand;
    }
}

TEST(InlinePassTest, KeepsCallSiteJumpTargetsAfterBodyExpansion) {
    MirChunk chunk = inline_pass_test_support::makeCallerChunk();

    InlinePass pass{ chunk };
    const std::size_t inlined = pass.run(8);

    ASSERT_EQ(inlined, 1u);
    ASSERT_EQ(chunk.code.size(), 6u);
    EXPECT_EQ(chunk.slotCount, 5);

    const int jump = inline_pass_test_support::indexOf(chunk, MirOp::JMP_IF_FALSE);
    const int halt = inline_pass_test_support::indexOf(chunk, MirOp::HALT);

    ASSERT_GE(jump, 0);
    ASSERT_GE(halt, 0);
    EXPECT_EQ(inline_pass_test_support::absoluteTarget(chunk, jump), halt);
}

TEST(InlinePassTest, EveryJumpTargetStaysInRangeAfterExpansion) {
    MirChunk chunk = inline_pass_test_support::makeCallerChunk();

    InlinePass pass{ chunk };
    ASSERT_EQ(pass.run(8), 1u);

    const int size = static_cast<int>(chunk.code.size());
    for (int i = 0; i < size; ++i) {
        if (chunk.code[i].op != MirOp::JMP && chunk.code[i].op != MirOp::JMP_IF_FALSE && chunk.code[i].op != MirOp::JMP_IF_TRUE)
            continue;

        const int target = inline_pass_test_support::absoluteTarget(chunk, i);
        EXPECT_GE(target, 0) << "jump at " << i;
        EXPECT_LE(target, size) << "jump at " << i;
    }
}

TEST(InlinePassTest, LeavesInstructionStreamUntouchedWhenNothingIsInlined) {
    MirChunk chunk;
    chunk.slotCount = 3;
    chunk.constants.push_back(1);
    chunk.code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::JMP_IF_FALSE, 1, -1, 0, -1 },
        { MirOp::HALT, 0 },
    };

    InlinePass pass{ chunk };
    const std::size_t inlined = pass.run(8);

    EXPECT_EQ(inlined, 0u);
    ASSERT_EQ(chunk.code.size(), 3u);
    EXPECT_EQ(chunk.code[1].op, MirOp::JMP_IF_FALSE);
    EXPECT_EQ(inline_pass_test_support::absoluteTarget(chunk, 1), 3);
    EXPECT_EQ(chunk.slotCount, 3);
}

TEST(InlinePassTest, ExpansionWithoutAnyBranchKeepsShape) {
    MirChunk chunk;
    chunk.slotCount = 2;
    chunk.constants.push_back(1);

    MethodMeta meta;
    meta.name = "leaf";
    meta.argCount = 0;
    meta.classIndex = -1;
    meta.bodyIndex = 0;
    chunk.methods.push_back(meta);

    auto body = std::make_unique<MirChunk>();
    body->slotCount = 1;
    body->code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::RETURN, 0, -1, 0 },
    };
    chunk.methodBodies.push_back(std::move(body));

    chunk.code = {
        { MirOp::CALL_METHOD, 0, 0, 0, -1, -1, 0 },
        { MirOp::HALT, 0 },
    };

    InlinePass pass{ chunk };
    ASSERT_EQ(pass.run(8), 1u);

    ASSERT_GE(chunk.code.size(), 2u);
    EXPECT_EQ(chunk.code.back().op, MirOp::HALT);
    EXPECT_EQ(inline_pass_test_support::indexOf(chunk, MirOp::CALL_METHOD), -1);
}
