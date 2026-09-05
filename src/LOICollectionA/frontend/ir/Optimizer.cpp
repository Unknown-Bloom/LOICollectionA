#include "LOICollectionA/frontend/ir/opt/OptContext.h"
#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"
#include "LOICollectionA/frontend/ir/opt/passes/ConstantFoldPass.h"
#include "LOICollectionA/frontend/ir/opt/passes/DeadCodePass.h"
#include "LOICollectionA/frontend/ir/opt/passes/DeadStorePass.h"
#include "LOICollectionA/frontend/ir/opt/passes/LICMPass.h"
#include "LOICollectionA/frontend/ir/opt/passes/CSEPass.h"
#include "LOICollectionA/frontend/ir/opt/passes/FusePass.h"

#include "LOICollectionA/frontend/ir/Optimizer.h"

namespace LOICollection::frontend::ir {
    Optimizer::Stats Optimizer::optimize(MirChunk& chunk) {
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

    Optimizer::Stats Optimizer::optimizeChunk(MirChunk& chunk) {
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

        if (this->enabled(Pass::Fuse)) {
            opt::FusePass fuse{ chunk };
            total.removed += fuse.run();
        }

        return total;
    }

    Optimizer::Stats Optimizer::optimizeChunkOnce(MirChunk& chunk) {
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
