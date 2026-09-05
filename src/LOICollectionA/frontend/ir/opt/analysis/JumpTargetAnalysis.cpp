#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    JumpTargetAnalysis::JumpTargetAnalysis(const std::vector<MirInstr>& code) {
        for (size_t i = 0; i < code.size(); ++i) {
            if (!isJump(code[i].op))
                continue;

            int target = static_cast<int>(i) + 1 + code[i].operand;
            if (target >= 0 && target < static_cast<int>(code.size())) {
                mTargets.insert(target);
                mJumpSources[target].push_back(static_cast<int>(i));
            }
        }
    }

    const std::vector<int>* JumpTargetAnalysis::sourcesOf(int index) const {
        auto it = mJumpSources.find(index);
        return it != mJumpSources.end() ? &it->second : nullptr;
    }
}
