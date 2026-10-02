#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/LoopAnalysis.h"

#include "common/frontend/MirTestSupport.h"

using namespace LOICollection::frontend::ir::opt;

TEST(LoopAnalysisTest, CountdownLoopHasHeaderBodyExitAndEntry) {
    auto chunk = LOICollection::tests::mir::makeCountdownLoopChunk();
    ControlFlowGraph cfg{ chunk.code };

    const std::vector<NaturalLoop> loops = findNaturalLoops(cfg);

    ASSERT_EQ(loops.size(), 1u);

    EXPECT_EQ(loops[0].header, 1);
    EXPECT_EQ(loops[0].blocks, std::vector<int>({ 1, 2 }));
    EXPECT_EQ(loops[0].exits, std::vector<int>({ 3 }));
    EXPECT_EQ(loops[0].entries, std::vector<int>({ 0 }));
}

TEST(LoopAnalysisTest, ContainsReportsMembership) {
    auto chunk = LOICollection::tests::mir::makeCountdownLoopChunk();
    ControlFlowGraph cfg{ chunk.code };

    const std::vector<NaturalLoop> loops = findNaturalLoops(cfg);
    ASSERT_EQ(loops.size(), 1u);

    EXPECT_TRUE(loops[0].contains(1));
    EXPECT_TRUE(loops[0].contains(2));
    EXPECT_FALSE(loops[0].contains(0));
    EXPECT_FALSE(loops[0].contains(3));
    EXPECT_FALSE(loops[0].contains(4));
}

TEST(LoopAnalysisTest, NestedLoopsAreReportedOuterFirst) {
    auto chunk = LOICollection::tests::mir::makeNestedLoopChunk();
    ControlFlowGraph cfg{ chunk.code };

    const std::vector<NaturalLoop> loops = findNaturalLoops(cfg);

    ASSERT_EQ(loops.size(), 2u);

    EXPECT_EQ(loops[0].header, 1);
    EXPECT_EQ(loops[0].blocks, std::vector<int>({ 1, 2, 3, 4, 5 }));
    EXPECT_EQ(loops[0].exits, std::vector<int>({ 6 }));
    EXPECT_EQ(loops[0].entries, std::vector<int>({ 0 }));

    EXPECT_EQ(loops[1].header, 3);
    EXPECT_EQ(loops[1].blocks, std::vector<int>({ 3, 4 }));
    EXPECT_EQ(loops[1].exits, std::vector<int>({ 5 }));
    EXPECT_EQ(loops[1].entries, std::vector<int>({ 2 }));
}

TEST(LoopAnalysisTest, InnerLoopIsContainedInOuterLoop) {
    auto chunk = LOICollection::tests::mir::makeNestedLoopChunk();
    ControlFlowGraph cfg{ chunk.code };

    const std::vector<NaturalLoop> loops = findNaturalLoops(cfg);
    ASSERT_EQ(loops.size(), 2u);

    for (const int block : loops[1].blocks)
        EXPECT_TRUE(loops[0].contains(block)) << "block " << block;

    EXPECT_FALSE(loops[1].contains(loops[0].header));
    EXPECT_TRUE(loops[0].contains(loops[1].header));
}

TEST(LoopAnalysisTest, StraightLineCodeHasNoLoops) {
    auto chunk = LOICollection::tests::mir::makeStraightLineChunk();
    ControlFlowGraph cfg{ chunk.code };

    EXPECT_TRUE(findNaturalLoops(cfg).empty());
}

TEST(LoopAnalysisTest, SelfJumpIsNotReportedAsALoop) {
    auto chunk = LOICollection::tests::mir::makeSelfJumpChunk();
    ControlFlowGraph cfg{ chunk.code };

    EXPECT_TRUE(findNaturalLoops(cfg).empty());
}

TEST(LoopAnalysisTest, EveryLoopBlockReachesItsHeader) {
    auto chunk = LOICollection::tests::mir::makeNestedLoopChunk();
    ControlFlowGraph cfg{ chunk.code };

    for (const NaturalLoop& loop : findNaturalLoops(cfg)) {
        ASSERT_FALSE(loop.blocks.empty());
        EXPECT_TRUE(loop.contains(loop.header));
        EXPECT_EQ(loop.blocks.front(), loop.header);
        EXPECT_TRUE(std::is_sorted(loop.blocks.begin(), loop.blocks.end()));

        for (const int block : loop.blocks)
            EXPECT_LT(block, cfg.size());

        for (const int exit : loop.exits) {
            EXPECT_FALSE(loop.contains(exit));
            EXPECT_LT(exit, cfg.size());
        }

        for (const int entry : loop.entries)
            EXPECT_FALSE(loop.contains(entry));
    }
}
