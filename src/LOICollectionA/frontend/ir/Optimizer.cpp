#include "LOICollectionA/frontend/ir/opt/OptContext.h"
#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"
#include "LOICollectionA/frontend/ir/opt/passes/ConstantFoldPass.h"
#include "LOICollectionA/frontend/ir/opt/passes/DeadCodePass.h"
#include "LOICollectionA/frontend/ir/opt/passes/DeadStorePass.h"
#include "LOICollectionA/frontend/ir/opt/passes/LICMPass.h"
#include "LOICollectionA/frontend/ir/opt/passes/CSEPass.h"

#include "LOICollectionA/frontend/ir/Optimizer.h"

namespace LOICollection::frontend::ir {
    Optimizer::Stats Optimizer::optimize(BytecodeChunk& chunk) {
        Stats total;

        Stats mainStats = this->optimizeChunk(chunk);
        total.folded += mainStats.folded;
        total.removed += mainStats.removed;

        for (auto& body : chunk.methodBodies) {
            Stats bodyStats = this->optimizeChunk(*body);
            total.folded += bodyStats.folded;
            total.removed += bodyStats.removed;
        }

        return total;
    }

    // Repeat the single pass until it stops making progress: each round's rewrites
    // (forwarded loads, folded branches, dropped operands) expose new opportunities
    // for the next round. A pass that changes nothing means the chunk is stable.
    // Hoisting runs after the fixpoint so that loops which turn out to be dead are
    // already gone, and runs before a last fixpoint so the peephole passes can tidy
    // up the DUP/POP pair the hoist leaves around the loop.
    Optimizer::Stats Optimizer::optimizeChunk(BytecodeChunk& chunk) {
        Stats total;

        for (int pass = 0; pass < 16; ++pass) {
            Stats once = this->optimizeChunkOnce(chunk);
            total.folded += once.folded;
            total.removed += once.removed;

            if (once.folded == 0 && once.removed == 0)
                break;
        }

        if (this->enabled(Pass::LICM)) {
            opt::LICMPass licm{ chunk };
            licm.run();
        }

        for (int pass = 0; pass < 16; ++pass) {
            Stats once = this->optimizeChunkOnce(chunk);
            total.folded += once.folded;
            total.removed += once.removed;

            if (once.folded == 0 && once.removed == 0)
                break;
        }

        return total;
    }

    Optimizer::Stats Optimizer::optimizeChunkOnce(BytecodeChunk& chunk) {
        if (chunk.code.empty())
            return {};

        const opt::JumpTargetAnalysis jumps{ chunk.code };

        opt::OptContext ctx{ chunk.code.size() };

        opt::ConstantFoldPass foldPass{ chunk, ctx, jumps };
        foldPass.run(this->enabled(Pass::ConstantFold));

        opt::DeadStorePass storePass{ chunk, ctx, jumps };
        storePass.run(this->enabled(Pass::DeadStore));

        opt::DeadCodePass deadPass;
        deadPass.run(chunk, ctx, this->enabled(Pass::DeadCode));

        opt::CSEPass csePass{ chunk };
        if (this->enabled(Pass::CSE)) {
            ctx.stats.removed += csePass.run();
        }

        return ctx.stats;
    }
}
