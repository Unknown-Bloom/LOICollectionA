#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LOICollectionA/frontend/ir/ByteCode.h"

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

namespace LOICollection::frontend::ir::opt {
    class JumpTargetAnalysis {
    public:
        explicit JumpTargetAnalysis(const std::vector<Instruction>& code);

        bool isTarget(int index) const { return mTargets.contains(index); }

        const std::vector<int>* sourcesOf(int index) const;

    private:
        std::unordered_set<int> mTargets;
        std::unordered_map<int, std::vector<int>> mJumpSources;
    };
}
