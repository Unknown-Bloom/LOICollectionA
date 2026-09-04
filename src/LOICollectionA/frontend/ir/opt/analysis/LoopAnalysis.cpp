#include <algorithm>
#include <map>
#include <utility>

#include "LOICollectionA/frontend/ir/opt/analysis/LoopAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        void depthFirstSearch(
            int block,
            const ControlFlowGraph& cfg,
            std::vector<int>& state,
            std::vector<std::pair<int, int>>& backEdges
        ) {
            state[block] = 1;

            for (const int successor : cfg.blocks()[block].successors) {
                if (successor < 0 || successor >= cfg.size())
                    continue;

                if (state[successor] == 1)
                    backEdges.emplace_back(block, successor);
                else if (state[successor] == 0)
                    depthFirstSearch(successor, cfg, state, backEdges);
            }

            state[block] = 2;
        }

        void append(std::vector<int>& into, int value) {
            if (std::ranges::find(into, value) == into.end())
                into.push_back(value);
        }
}

    bool NaturalLoop::contains(int block) const {
        return std::ranges::find(blocks, block) != blocks.end();
    }

    std::vector<NaturalLoop> findNaturalLoops(const ControlFlowGraph& cfg) {
        std::vector<std::pair<int, int>> backEdges;
        std::vector<int> state(cfg.size(), 0);

        for (int block = 0; block < cfg.size(); ++block)
            if (state[block] == 0)
                depthFirstSearch(block, cfg, state, backEdges);

        std::map<int, NaturalLoop> loops;

        for (const auto& [tail, header] : backEdges) {
            NaturalLoop& loop = loops[header];
            loop.header = header;

            if (loop.blocks.empty())
                loop.blocks.push_back(header);

            std::vector<int> pending{ tail };
            while (!pending.empty()) {
                const int block = pending.back();
                pending.pop_back();

                if (loop.contains(block))
                    continue;

                loop.blocks.push_back(block);

                for (const int predecessor : cfg.blocks()[block].predecessors)
                    pending.push_back(predecessor);
            }
        }

        std::vector<NaturalLoop> out;
        for (auto& [header, loop] : loops) {
            std::ranges::sort(loop.blocks);

            for (const int block : loop.blocks)
                for (const int successor : cfg.blocks()[block].successors)
                    if (!loop.contains(successor))
                        append(loop.exits, successor);

            for (const int predecessor : cfg.blocks()[header].predecessors)
                if (!loop.contains(predecessor))
                    append(loop.entries, predecessor);

            out.push_back(std::move(loop));
        }

        return out;
    }
}
