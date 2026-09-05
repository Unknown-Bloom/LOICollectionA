#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "LOICollectionA/frontend/ir/Mir.h"

#include "LOICollectionA/frontend/ir/opt/OptContext.h"
#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    // Removes store-to-slot/store-to-variable instructions whose value is never
    // read before being overwritten. A store becomes dead when a later store to
    // the same location lands before any intervening load, call, or jump target.
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
