#include <string>
#include <variant>
#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/ControlFlowGraph.h"
#include "LOICollectionA/frontend/ir/opt/analysis/FoldPredicates.h"
#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/ConstantPropagationPass.h"

namespace LOICollection::frontend::ir::opt::detail {
    template <typename Map>
    void intersect(Map& into, const Map& from) {
        for (auto it = into.begin(); it != into.end();) {
            const auto other = from.find(it->first);

            if (other == from.end() || it->second != other->second)
                it = into.erase(it);
            else
                ++it;
        }
    }

    template <typename Map>
    void eraseChanged(Map& seed, const Map& next) {
        for (auto it = seed.begin(); it != seed.end();) {
            const auto other = next.find(it->first);

            if (other == next.end() || it->second != other->second)
                it = seed.erase(it);
            else
                ++it;
        }
    }

    bool leadsToSelf(const ControlFlowGraph::Block& block, const std::vector<MirInstr>& code) {
        if (block.end <= block.begin)
            return false;

        const int last = block.end - 1;
        if (!isJump(code[last].op))
            return false;

        return last + 1 + code[last].operand == block.begin;
    }
}

namespace LOICollection::frontend::ir::opt {
    using namespace detail;

    void ConstantPropagationPass::step(const MirInstr& instr, State& state) const {
        if (canWriteVariables(instr.op)) {
            state.slots.clear();
            state.names.clear();
        }

        switch (instr.op) {
            case MirOp::LOAD_CONST: {
                const auto& value = mChunk.constants[instr.operand];
                if (instr.dst < 0)
                    return;

                if (isScalarValue(value))
                    state.regs[instr.dst] = value;
                else
                    state.regs.erase(instr.dst);
                return;
            }

            case MirOp::MOVE: {
                if (instr.dst < 0)
                    return;

                const auto it = state.regs.find(instr.src1);
                if (it == state.regs.end())
                    state.regs.erase(instr.dst);
                else
                    state.regs[instr.dst] = it->second;
                return;
            }

            case MirOp::LOAD_SLOT: {
                const auto it = state.slots.find(instr.operand);
                if (it == state.slots.end())
                    state.regs.erase(instr.dst);
                else
                    state.regs[instr.dst] = it->second;
                return;
            }

            case MirOp::LOAD_VAR: {
                const auto* name = std::get_if<std::string>(&mChunk.constants[instr.operand]);
                const auto it = name ? state.names.find(*name) : state.names.end();

                if (it == state.names.end())
                    state.regs.erase(instr.dst);
                else
                    state.regs[instr.dst] = it->second;
                return;
            }

            case MirOp::STORE_SLOT: {
                const auto it = state.regs.find(instr.src1);
                if (it == state.regs.end())
                    state.slots.erase(instr.operand);
                else
                    state.slots[instr.operand] = it->second;
                return;
            }

            case MirOp::STORE_VAR: {
                const auto* name = std::get_if<std::string>(&mChunk.constants[instr.operand]);
                if (!name)
                    return;

                const auto it = state.regs.find(instr.src1);
                if (it == state.regs.end())
                    state.names.erase(*name);
                else
                    state.names[*name] = it->second;
                return;
            }

            default:
                if (instr.dst >= 0)
                    state.regs.erase(instr.dst);
                return;
        }
    }

    void ConstantPropagationPass::transfer(int begin, int end, State& state) const {
        for (int i = begin; i < end; ++i)
            this->step(mChunk.code[i], state);
    }

    ConstantPropagationPass::State ConstantPropagationPass::merge(
        const std::vector<const State*>& inputs
    ) const {
        if (inputs.empty())
            return {};

        State result = *inputs.front();

        for (size_t i = 1; i < inputs.size(); ++i) {
            intersect(result.regs, inputs[i]->regs);
            intersect(result.slots, inputs[i]->slots);
            intersect(result.names, inputs[i]->names);
        }

        return result;
    }

    size_t ConstantPropagationPass::run() {
        if (mChunk.code.empty())
            return 0;

        const ControlFlowGraph cfg{ mChunk.code };
        std::vector<BlockState> states(cfg.blocks().size());

        for (int round = 0; round < 64; ++round) {
            bool changed = false;

            for (int index = 0; index < cfg.size(); ++index) {
                const auto& block = cfg.blocks()[index];
                BlockState& current = states[index];

                std::vector<const State*> inputs;
                for (const int predecessor : block.predecessors)
                    inputs.push_back(&states[predecessor].out);

                State in = index == 0 ? State{} : this->merge(inputs);

                if (leadsToSelf(block, mChunk.code)) {
                    for (int iteration = 0; iteration < 8; ++iteration) {
                        State next = in;
                        this->transfer(block.begin, block.end, next);

                        const State previous = in;
                        eraseChanged(in.regs, next.regs);
                        eraseChanged(in.slots, next.slots);
                        eraseChanged(in.names, next.names);

                        if (in.regs == previous.regs && in.slots == previous.slots &&
                            in.names == previous.names)
                            break;
                    }
                }

                State out = in;
                this->transfer(block.begin, block.end, out);

                if (in.regs != current.in.regs || in.slots != current.in.slots ||
                    in.names != current.in.names) {
                    current.in = std::move(in);
                    changed = true;
                }

                if (out.regs != current.out.regs || out.slots != current.out.slots ||
                    out.names != current.out.names) {
                    current.out = std::move(out);
                    changed = true;
                }
            }

            if (!changed)
                break;
        }

        size_t folded = 0;

        for (int index = 0; index < cfg.size(); ++index) {
            const auto& block = cfg.blocks()[index];
            State state = states[index].in;

            for (int i = block.begin; i < block.end; ++i) {
                MirInstr& instr = mChunk.code[i];
                const ValueNode::ValueType* known = nullptr;

                if (instr.op == MirOp::LOAD_SLOT) {
                    const auto it = state.slots.find(instr.operand);
                    if (it != state.slots.end())
                        known = &it->second;
                } else if (instr.op == MirOp::LOAD_VAR) {
                    const auto* name = std::get_if<std::string>(&mChunk.constants[instr.operand]);
                    const auto it = name ? state.names.find(*name) : state.names.end();

                    if (it != state.names.end())
                        known = &it->second;
                }

                if (known) {
                    instr.op = MirOp::LOAD_CONST;
                    instr.operand = addConstant(mChunk, *known);
                    instr.src1 = -1;
                    ++folded;
                }

                this->step(instr, state);
            }
        }

        return folded;
    }
}
