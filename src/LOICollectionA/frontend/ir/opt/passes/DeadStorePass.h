#pragma once

#include <string>
#include <unordered_map>
#include <variant>

#include "LOICollectionA/frontend/ir/Mir.h"

#include "LOICollectionA/frontend/ir/opt/OptContext.h"
#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    class DeadStorePass {
    public:
        DeadStorePass(MirChunk& chunk, OptContext& ctx, const JumpTargetAnalysis& jumps)
        : mChunk(chunk),
          mCtx(ctx),
          mJumps(jumps) {}

        void run(bool enabled);

    private:
        using VariableKey = std::variant<int, std::string>;

        void recordStore(const VariableKey& key, int at, int slot);

        void killStore(int at);

        MirChunk& mChunk;
        OptContext& mCtx;
        const JumpTargetAnalysis& mJumps;
        std::unordered_map<VariableKey, int> mPending;
    };
}
