#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/ControlFlowGraph.h"

#include "common/frontend/MirTestSupport.h"

using namespace LOICollection::frontend::ir;
using namespace LOICollection::frontend::ir::opt;

TEST(ControlFlowGraphTest, CountdownLoopSplitsIntoFiveBlocks) {
    auto chunk = LOICollection::tests::mir::makeCountdownLoopChunk();
    ControlFlowGraph cfg{ chunk.code };

    ASSERT_EQ(cfg.size(), 5);

    EXPECT_EQ(cfg.blocks()[0].begin, 0);
    EXPECT_EQ(cfg.blocks()[0].end, 2);
    EXPECT_EQ(cfg.blocks()[1].begin, 2);
    EXPECT_EQ(cfg.blocks()[1].end, 6);
    EXPECT_EQ(cfg.blocks()[2].begin, 6);
    EXPECT_EQ(cfg.blocks()[2].end, 8);
    EXPECT_EQ(cfg.blocks()[3].begin, 8);
    EXPECT_EQ(cfg.blocks()[3].end, 9);
    EXPECT_EQ(cfg.blocks()[4].begin, 9);
    EXPECT_EQ(cfg.blocks()[4].end, 9);
}

TEST(ControlFlowGraphTest, CountdownLoopLinksBranchesAndBackEdge) {
    auto chunk = LOICollection::tests::mir::makeCountdownLoopChunk();
    ControlFlowGraph cfg{ chunk.code };

    EXPECT_EQ(cfg.blocks()[0].successors, std::vector<int>({ 1 }));
    EXPECT_EQ(cfg.blocks()[0].predecessors, std::vector<int>());

    EXPECT_EQ(cfg.blocks()[1].successors, std::vector<int>({ 3, 2 }));
    EXPECT_EQ(cfg.blocks()[1].predecessors, std::vector<int>({ 0, 2 }));

    EXPECT_EQ(cfg.blocks()[2].successors, std::vector<int>({ 1 }));
    EXPECT_EQ(cfg.blocks()[2].predecessors, std::vector<int>({ 1 }));

    EXPECT_TRUE(cfg.blocks()[3].successors.empty());
    EXPECT_EQ(cfg.blocks()[3].predecessors, std::vector<int>({ 1 }));

    EXPECT_TRUE(cfg.blocks()[4].successors.empty());
    EXPECT_TRUE(cfg.blocks()[4].predecessors.empty());
}

TEST(ControlFlowGraphTest, BlockOfMapsEveryInstructionIndex) {
    auto chunk = LOICollection::tests::mir::makeCountdownLoopChunk();
    ControlFlowGraph cfg{ chunk.code };

    EXPECT_EQ(cfg.blockOf(0), 0);
    EXPECT_EQ(cfg.blockOf(1), 0);
    EXPECT_EQ(cfg.blockOf(2), 1);
    EXPECT_EQ(cfg.blockOf(5), 1);
    EXPECT_EQ(cfg.blockOf(6), 2);
    EXPECT_EQ(cfg.blockOf(7), 2);
    EXPECT_EQ(cfg.blockOf(8), 3);
    EXPECT_EQ(cfg.blockOf(9), 4);
}

TEST(ControlFlowGraphTest, BlockOfRejectsOutOfRangeIndices) {
    auto chunk = LOICollection::tests::mir::makeCountdownLoopChunk();
    ControlFlowGraph cfg{ chunk.code };

    EXPECT_EQ(cfg.blockOf(-1), -1);
    EXPECT_EQ(cfg.blockOf(10), -1);
    EXPECT_EQ(cfg.blockOf(1000), -1);
}

TEST(ControlFlowGraphTest, StraightLineCodeIsOneBlockPlusTrailingEmptyBlock) {
    auto chunk = LOICollection::tests::mir::makeStraightLineChunk();
    ControlFlowGraph cfg{ chunk.code };

    ASSERT_EQ(cfg.size(), 2);

    EXPECT_EQ(cfg.blocks()[0].begin, 0);
    EXPECT_EQ(cfg.blocks()[0].end, 3);
    EXPECT_TRUE(cfg.blocks()[0].successors.empty());
    EXPECT_TRUE(cfg.blocks()[0].predecessors.empty());

    EXPECT_EQ(cfg.blocks()[1].begin, 3);
    EXPECT_EQ(cfg.blocks()[1].end, 3);
    EXPECT_TRUE(cfg.blocks()[1].successors.empty());
    EXPECT_TRUE(cfg.blocks()[1].predecessors.empty());

    EXPECT_EQ(cfg.blockOf(0), 0);
    EXPECT_EQ(cfg.blockOf(2), 0);
    EXPECT_EQ(cfg.blockOf(3), 1);
    EXPECT_EQ(cfg.blockOf(4), -1);
}

TEST(ControlFlowGraphTest, ConditionalJumpLinksTargetAndFallthrough) {
    auto chunk = LOICollection::tests::mir::makeConditionalFallthroughChunk();
    ControlFlowGraph cfg{ chunk.code };

    ASSERT_EQ(cfg.size(), 3);

    EXPECT_EQ(cfg.blocks()[0].successors, std::vector<int>({ 2, 1 }));
    EXPECT_TRUE(cfg.blocks()[0].predecessors.empty());

    EXPECT_EQ(cfg.blocks()[1].successors, std::vector<int>({ 2 }));
    EXPECT_EQ(cfg.blocks()[1].predecessors, std::vector<int>({ 0 }));

    EXPECT_EQ(cfg.blocks()[2].predecessors, std::vector<int>({ 0, 1 }));
}

TEST(ControlFlowGraphTest, CodeWithoutTerminatorOrJumpIsOneBlock) {
    std::vector<MirInstr> code(2);
    code[0].op = MirOp::LOAD_CONST;
    code[1].op = MirOp::LOAD_CONST;

    ControlFlowGraph cfg{ code };

    ASSERT_EQ(cfg.size(), 1);
    EXPECT_EQ(cfg.blocks()[0].begin, 0);
    EXPECT_EQ(cfg.blocks()[0].end, 2);
    EXPECT_TRUE(cfg.blocks()[0].successors.empty());
    EXPECT_TRUE(cfg.blocks()[0].predecessors.empty());
    EXPECT_EQ(cfg.blockOf(2), 0);
}

TEST(ControlFlowGraphTest, EmptyCodeProducesOneEmptyBlock) {
    const std::vector<MirInstr> code;
    ControlFlowGraph cfg{ code };

    ASSERT_EQ(cfg.size(), 1);
    EXPECT_EQ(cfg.blocks()[0].begin, 0);
    EXPECT_EQ(cfg.blocks()[0].end, 0);
    EXPECT_EQ(cfg.blockOf(0), 0);
    EXPECT_EQ(cfg.blockOf(1), -1);
}

TEST(ControlFlowGraphTest, UnconditionalJumpHasNoFallthroughSuccessor) {
    auto chunk = LOICollection::tests::mir::makeUnconditionalJumpChunk();
    ControlFlowGraph cfg{ chunk.code };

    ASSERT_EQ(cfg.size(), 4);

    EXPECT_EQ(cfg.blocks()[0].successors, std::vector<int>({ 2 }));
    EXPECT_TRUE(cfg.blocks()[0].predecessors.empty());

    EXPECT_EQ(cfg.blocks()[1].successors, std::vector<int>({ 2 }));
    EXPECT_TRUE(cfg.blocks()[1].predecessors.empty());

    EXPECT_EQ(cfg.blocks()[2].predecessors, std::vector<int>({ 0, 1 }));
}

TEST(ControlFlowGraphTest, SelfJumpDoesNotCreateSelfEdge) {
    auto chunk = LOICollection::tests::mir::makeSelfJumpChunk();
    ControlFlowGraph cfg{ chunk.code };

    ASSERT_EQ(cfg.size(), 3);

    EXPECT_EQ(cfg.blocks()[0].successors, std::vector<int>({ 1 }));
    EXPECT_TRUE(cfg.blocks()[1].successors.empty());
    EXPECT_EQ(cfg.blocks()[1].predecessors, std::vector<int>({ 0 }));
    EXPECT_TRUE(cfg.blocks()[2].predecessors.empty());
}

TEST(ControlFlowGraphTest, PredecessorListsMirrorSuccessors) {
    auto chunk = LOICollection::tests::mir::makeNestedLoopChunk();
    ControlFlowGraph cfg{ chunk.code };

    for (int block = 0; block < cfg.size(); ++block) {
        for (const int successor : cfg.blocks()[block].successors) {
            ASSERT_LT(successor, cfg.size());
            const auto& predecessors = cfg.blocks()[successor].predecessors;
            EXPECT_NE(std::find(predecessors.begin(), predecessors.end(), block), predecessors.end())
                << "block " << block << " -> " << successor;
        }

        for (const int predecessor : cfg.blocks()[block].predecessors) {
            ASSERT_LT(predecessor, cfg.size());
            const auto& successors = cfg.blocks()[predecessor].successors;
            EXPECT_NE(std::find(successors.begin(), successors.end(), block), successors.end())
                << "block " << predecessor << " -> " << block;
        }
    }
}
