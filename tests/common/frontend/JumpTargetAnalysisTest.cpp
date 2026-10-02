#include <gtest/gtest.h>

#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"

#include "common/frontend/MirTestSupport.h"

using namespace LOICollection::frontend::ir;
using namespace LOICollection::frontend::ir::opt;

namespace jump_target_test_support {
    MirChunk makeSharedTargetChunk() {
        MirChunk chunk;
        chunk.emit(MirOp::LOAD_CONST, 0);
        chunk.emit(MirOp::JMP_IF_FALSE, 3);
        chunk.emit(MirOp::LOAD_CONST, 1);
        chunk.emit(MirOp::JMP, 1);
        chunk.emit(MirOp::LOAD_CONST, 2);
        chunk.emit(MirOp::RETURN);
        return chunk;
    }

    MirChunk makeForwardOutOfRangeChunk() {
        MirChunk chunk;
        chunk.emit(MirOp::JMP, 100);
        chunk.emit(MirOp::RETURN);
        return chunk;
    }

    MirChunk makeBackwardOutOfRangeChunk() {
        MirChunk chunk;
        chunk.emit(MirOp::JMP, -5);
        chunk.emit(MirOp::RETURN);
        return chunk;
    }

    MirChunk makeJumpToFinalIndexChunk() {
        MirChunk chunk;
        chunk.emit(MirOp::LOAD_CONST, 0);
        chunk.emit(MirOp::JMP_IF_FALSE, 1);
        chunk.emit(MirOp::LOAD_CONST, 1);
        chunk.emit(MirOp::RETURN);
        return chunk;
    }

    MirChunk makeJumpPastEndChunk() {
        MirChunk chunk;
        chunk.emit(MirOp::LOAD_CONST, 0);
        chunk.emit(MirOp::JMP_IF_FALSE, 2);
        chunk.emit(MirOp::LOAD_CONST, 1);
        chunk.emit(MirOp::RETURN);
        return chunk;
    }
}

TEST(JumpTargetAnalysisTest, RecordsConditionalAndUnconditionalTargets) {
    auto chunk = LOICollection::tests::mir::makeCountdownLoopChunk();
    JumpTargetAnalysis jumps{ chunk.code };

    EXPECT_TRUE(jumps.isTarget(2));
    EXPECT_TRUE(jumps.isTarget(8));

    EXPECT_FALSE(jumps.isTarget(0));
    EXPECT_FALSE(jumps.isTarget(6));
    EXPECT_FALSE(jumps.isTarget(9));
}

TEST(JumpTargetAnalysisTest, SourcesAreListedInInstructionOrder) {
    auto chunk = LOICollection::tests::mir::makeCountdownLoopChunk();
    JumpTargetAnalysis jumps{ chunk.code };

    const std::vector<int>* toHeader = jumps.sourcesOf(2);
    ASSERT_NE(toHeader, nullptr);
    EXPECT_EQ(*toHeader, std::vector<int>({ 7 }));

    const std::vector<int>* toExit = jumps.sourcesOf(8);
    ASSERT_NE(toExit, nullptr);
    EXPECT_EQ(*toExit, std::vector<int>({ 5 }));
}

TEST(JumpTargetAnalysisTest, NonTargetsHaveNoSourceList) {
    auto chunk = LOICollection::tests::mir::makeCountdownLoopChunk();
    JumpTargetAnalysis jumps{ chunk.code };

    EXPECT_EQ(jumps.sourcesOf(0), nullptr);
    EXPECT_EQ(jumps.sourcesOf(6), nullptr);
    EXPECT_EQ(jumps.sourcesOf(100), nullptr);
    EXPECT_EQ(jumps.sourcesOf(-1), nullptr);
}

TEST(JumpTargetAnalysisTest, MultipleJumpsShareOneTarget) {
    auto chunk = jump_target_test_support::makeSharedTargetChunk();
    JumpTargetAnalysis jumps{ chunk.code };

    EXPECT_TRUE(jumps.isTarget(5));

    const std::vector<int>* sources = jumps.sourcesOf(5);
    ASSERT_NE(sources, nullptr);
    EXPECT_EQ(*sources, std::vector<int>({ 1, 3 }));
}

TEST(JumpTargetAnalysisTest, OutOfRangeTargetsAreIgnored) {
    auto forward = jump_target_test_support::makeForwardOutOfRangeChunk();
    JumpTargetAnalysis forwardJumps{ forward.code };
    EXPECT_FALSE(forwardJumps.isTarget(101));
    EXPECT_EQ(forwardJumps.sourcesOf(101), nullptr);

    auto backward = jump_target_test_support::makeBackwardOutOfRangeChunk();
    JumpTargetAnalysis backwardJumps{ backward.code };
    EXPECT_FALSE(backwardJumps.isTarget(-4));
    EXPECT_EQ(backwardJumps.sourcesOf(-4), nullptr);
}

TEST(JumpTargetAnalysisTest, JumpOntoFinalIndexIsRecorded) {
    auto chunk = jump_target_test_support::makeJumpToFinalIndexChunk();
    JumpTargetAnalysis jumps{ chunk.code };

    EXPECT_TRUE(jumps.isTarget(3));

    const std::vector<int>* sources = jumps.sourcesOf(3);
    ASSERT_NE(sources, nullptr);
    EXPECT_EQ(*sources, std::vector<int>({ 1 }));
}

TEST(JumpTargetAnalysisTest, JumpPastEndIsIgnored) {
    auto chunk = jump_target_test_support::makeJumpPastEndChunk();
    JumpTargetAnalysis jumps{ chunk.code };

    EXPECT_FALSE(jumps.isTarget(4));
    EXPECT_EQ(jumps.sourcesOf(4), nullptr);
}

TEST(JumpTargetAnalysisTest, SelfJumpTargetsItself) {
    auto chunk = LOICollection::tests::mir::makeSelfJumpChunk();
    JumpTargetAnalysis jumps{ chunk.code };

    EXPECT_TRUE(jumps.isTarget(1));

    const std::vector<int>* sources = jumps.sourcesOf(1);
    ASSERT_NE(sources, nullptr);
    EXPECT_EQ(*sources, std::vector<int>({ 1 }));
}
