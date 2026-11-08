#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

#include "LOICollectionA/frontend/ir/mir/MirPass.h"

namespace LOICollection::frontend::ir::mir {
    namespace detail {
        void removeUnreachable(Body& body) {
            if (body.blocks.empty())
                return;

            std::vector<bool> reachable(body.blocks.size(), false);
            std::queue<BlockId> pending;
            pending.push(body.entry);
            reachable[body.entry] = true;

            const auto mark = [&reachable, &pending](BlockId block) -> void {
                if (block >= reachable.size() || reachable[block])
                    return;
                reachable[block] = true;
                pending.push(block);
            };

            while (!pending.empty()) {
                const BlockId block = pending.front();
                pending.pop();

                const Terminator& terminator = body.blocks[block].terminator;
                switch (terminator.kind) {
                    case TerminatorKind::Goto:
                        mark(terminator.target);
                        break;
                    case TerminatorKind::Branch:
                    case TerminatorKind::SwitchTag:
                        mark(terminator.target);
                        mark(terminator.alternate);
                        break;
                    default:
                        break;
                }
            }

            std::vector<BlockId> remap(body.blocks.size(), invalidId);
            std::vector<Block> blocks;
            blocks.reserve(body.blocks.size());

            for (std::size_t index = 0; index < body.blocks.size(); ++index) {
                if (!reachable[index])
                    continue;

                remap[index] = static_cast<BlockId>(blocks.size());
                blocks.push_back(std::move(body.blocks[index]));
            }

            for (Block& block : blocks) {
                Terminator& terminator = block.terminator;
                switch (terminator.kind) {
                    case TerminatorKind::Goto:
                        terminator.target = remap[terminator.target];
                        break;
                    case TerminatorKind::Branch:
                    case TerminatorKind::SwitchTag:
                        terminator.target = remap[terminator.target];
                        terminator.alternate = remap[terminator.alternate];
                        break;
                    default:
                        break;
                }
            }

            body.blocks = std::move(blocks);
            body.entry = 0;
        }
    }

    void MirPass::removeUnreachable(Module& module) {
        for (Body& body : module.bodies)
            detail::removeUnreachable(body);
    }

    void MirPass::run(Module& module) {
        removeUnreachable(module);
    }
}
