#include <string>
#include <unordered_map>
#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"
#include "LOICollectionA/frontend/ir/opt/analysis/ControlFlowGraph.h"

#include "LOICollectionA/frontend/ir/opt/passes/CSEPass.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        struct Val {
            std::string key;
            int start = 0;
            std::vector<int> readSlots;
            bool atom = false;
        };

        struct Avail {
            int slot = -1;
            int firstOpPos = -1;
            bool spilled = false;
            std::vector<int> readSlots;
        };

        bool isBarrier(OpCode op) {
            return canWriteVariables(op)
                || op == OpCode::LOAD_FIELD
                || op == OpCode::LOAD_INDEX
                || op == OpCode::STORE_INDEX
                || op == OpCode::MAKE_ARRAY
                || op == OpCode::MAKE_LAMBDA;
        }

        bool isPureBinary(OpCode op) {
            switch (op) {
                case OpCode::ADD:
                case OpCode::SUB:
                case OpCode::MUL:
                case OpCode::DIV:
                case OpCode::MOD:
                case OpCode::POW:
                case OpCode::CMP_EQ:
                case OpCode::CMP_NE:
                case OpCode::CMP_GT:
                case OpCode::CMP_LT:
                case OpCode::CMP_GE:
                case OpCode::CMP_LE:
                case OpCode::ADD_I:
                case OpCode::SUB_I:
                case OpCode::MUL_I:
                case OpCode::MOD_I:
                case OpCode::CMP_EQ_I:
                case OpCode::CMP_NE_I:
                case OpCode::CMP_GT_I:
                case OpCode::CMP_LT_I:
                case OpCode::CMP_GE_I:
                case OpCode::CMP_LE_I:
                    return true;
                default:
                    return false;
            }
        }

        bool isPureUnary(OpCode op) {
            switch (op) {
                case OpCode::NEG:
                case OpCode::NEG_I:
                case OpCode::NOT:
                case OpCode::INSTANCEOF:
                case OpCode::LOAD_LEN:
                case OpCode::DUP_IS_NONE:
                    return true;
                default:
                    return false;
            }
        }

        bool isLeaf(OpCode op) {
            switch (op) {
                case OpCode::LOAD_SLOT:
                case OpCode::LOAD_VAR:
                case OpCode::LOAD_THIS:
                case OpCode::PUSH_INT:
                case OpCode::PUSH_FLOAT:
                case OpCode::PUSH_STR:
                case OpCode::PUSH_BOOL:
                case OpCode::PUSH_NONE:
                    return true;
                default:
                    return false;
            }
        }

        std::string opName(OpCode op) {
            switch (op) {
                case OpCode::ADD: return "ADD";
                case OpCode::SUB: return "SUB";
                case OpCode::MUL: return "MUL";
                case OpCode::DIV: return "DIV";
                case OpCode::MOD: return "MOD";
                case OpCode::POW: return "POW";
                case OpCode::CMP_EQ: return "EQ";
                case OpCode::CMP_NE: return "NE";
                case OpCode::CMP_GT: return "GT";
                case OpCode::CMP_LT: return "LT";
                case OpCode::CMP_GE: return "GE";
                case OpCode::CMP_LE: return "LE";
                case OpCode::NEG: return "NEG";
                case OpCode::NOT: return "NOT";
                case OpCode::ADD_I: return "ADDI";
                case OpCode::SUB_I: return "SUBI";
                case OpCode::MUL_I: return "MULI";
                case OpCode::MOD_I: return "MODI";
                case OpCode::CMP_EQ_I: return "EQI";
                case OpCode::CMP_NE_I: return "NEI";
                case OpCode::CMP_GT_I: return "GTI";
                case OpCode::CMP_LT_I: return "LTI";
                case OpCode::CMP_GE_I: return "GEI";
                case OpCode::CMP_LE_I: return "LEI";
                case OpCode::NEG_I: return "NEGI";
                case OpCode::INSTANCEOF: return "INST";
                case OpCode::LOAD_LEN: return "LEN";
                case OpCode::DUP_IS_NONE: return "ISNONE";
                default: return "?";
            }
        }

        std::string constKey(OpCode op, int operand) {
            switch (op) {
                case OpCode::PUSH_INT: return "I:" + std::to_string(operand);
                case OpCode::PUSH_FLOAT: return "F:" + std::to_string(operand);
                case OpCode::PUSH_STR: return "S:" + std::to_string(operand);
                case OpCode::PUSH_BOOL: return "B:" + std::to_string(operand);
                case OpCode::PUSH_NONE: return "N";
                default: return "?";
            }
        }

        void apply(std::vector<Instruction>& code, std::vector<CSEPass::Edit>& edits) {
            std::sort(edits.begin(), edits.end(),
                [](const CSEPass::Edit& a, const CSEPass::Edit& b) { return a.at < b.at; });

            const int n = static_cast<int>(code.size());

            std::vector<Instruction> rewritten;
            rewritten.reserve(code.size());

            std::vector<int> oldToNew(n, -1);
            std::vector<int> origin;
            std::vector<std::vector<int>> placed(edits.size());

            for (int pos = 0; pos <= n; ++pos) {
                bool erased = false;
                int redirect = -1;

                for (size_t e = 0; e < edits.size(); ++e) {
                    if (edits[e].at == pos) {
                        for (const Instruction& ins : edits[e].insert) {
                            placed[e].push_back(static_cast<int>(rewritten.size()));
                            rewritten.push_back(ins);
                            origin.push_back(-1);
                        }
                    }

                    if (pos >= edits[e].at && pos < edits[e].eraseTo) {
                        erased = true;
                        if (!placed[e].empty())
                            redirect = placed[e].front();
                    }
                }

                if (pos == n)
                    break;

                if (erased) {
                    if (redirect >= 0)
                        oldToNew[pos] = redirect;
                    continue;
                }

                oldToNew[pos] = static_cast<int>(rewritten.size());
                origin.push_back(pos);
                rewritten.push_back(code[pos]);
            }

            for (size_t k = 0; k < rewritten.size(); ++k) {
                if (origin[k] < 0 || !isJump(rewritten[k].op))
                    continue;

                const int oldTarget = origin[k] + 1 + rewritten[k].operand;

                int newTarget = static_cast<int>(rewritten.size());
                if (oldTarget >= 0 && oldTarget < n) {
                    newTarget = oldToNew[oldTarget];
                    for (int j = oldTarget; j < n && newTarget < 0; ++j)
                        newTarget = oldToNew[j];
                }

                rewritten[k].operand = newTarget - static_cast<int>(k) - 1;
            }

            code = std::move(rewritten);
        }
    }

    size_t CSEPass::run() {
        ControlFlowGraph cfg{ mChunk.code };

        int nextSlot = mChunk.slotCount;
        std::vector<Instruction> out;
        out.reserve(mChunk.code.size());
        std::vector<Edit> edits;
        size_t eliminated = 0;

        for (const ControlFlowGraph::Block& block : cfg.blocks()) {
            std::vector<Val> vstack;
            std::unordered_map<std::string, Avail> avail;

            auto invalidate = [&](int slot) {
                for (auto it = avail.begin(); it != avail.end();) {
                    bool hit = false;
                    for (int s : it->second.readSlots)
                        if (s == slot) {
                            hit = true;
                            break;
                        }
                    if (hit)
                        it = avail.erase(it);
                    else
                        ++it;
                }
            };

            auto pop = [&]() -> Val {
                if (vstack.empty())
                    return Val{};
                Val v = vstack.back();
                vstack.pop_back();
                return v;
            };

            for (int i = block.begin; i < block.end; ++i) {
                const Instruction& ins = mChunk.code[i];
                const OpCode op = ins.op;
                const SourceLocation loc = ins.loc;

                if (isLeaf(op)) {
                    std::string key;
                    std::vector<int> rs;

                    if (op == OpCode::LOAD_SLOT || op == OpCode::LOAD_VAR) {
                        key = (op == OpCode::LOAD_SLOT ? "L" : "V") + std::to_string(ins.operand);
                        rs.push_back(ins.operand);
                    } else if (op == OpCode::LOAD_THIS) {
                        key = "THIS";
                    } else {
                        key = constKey(op, ins.operand);
                    }

                    const int start = static_cast<int>(out.size());
                    out.push_back(ins);
                    vstack.push_back(Val{ key, start, rs, true });
                    continue;
                }

                if (op == OpCode::DUP) {
                    if (!vstack.empty())
                        vstack.push_back(vstack.back());
                    out.push_back(ins);
                    continue;
                }

                if (op == OpCode::POP) {
                    if (!vstack.empty())
                        vstack.pop_back();
                    out.push_back(ins);
                    continue;
                }

                if (op == OpCode::STORE_SLOT || op == OpCode::STORE_VAR) {
                    out.push_back(ins);
                    pop();
                    invalidate(ins.operand);
                    continue;
                }

                if (isPureBinary(op)) {
                    Val right = pop();
                    Val left = pop();

                    if (left.key.empty() || right.key.empty()) {
                        out.push_back(ins);
                        vstack.push_back(Val{});
                        continue;
                    }

                    std::string key = opName(op) + "(" + left.key + "," + right.key + ")";
                    std::vector<int> rs = left.readSlots;
                    rs.insert(rs.end(), right.readSlots.begin(), right.readSlots.end());
                    const int curStart = std::min(left.start, right.start);

                    auto it = avail.find(key);
                    if (it != avail.end() && it->second.spilled && left.atom && right.atom) {
                        edits.push_back({ curStart, static_cast<int>(out.size()) + 1,
                            { Instruction{ OpCode::LOAD_SLOT, it->second.slot, loc } } });
                        out.push_back(ins);
                        ++eliminated;
                        vstack.push_back(Val{ key, curStart, rs, true });
                    } else {
                        if (it != avail.end() && left.atom && right.atom) {
                            const int s = nextSlot++;
                            it->second.slot = s;
                            it->second.spilled = true;
                            edits.push_back({ it->second.firstOpPos + 1, -1,
                                { Instruction{ OpCode::DUP_STORE_SLOT, s, mChunk.code[it->second.firstOpPos].loc } } });
                            edits.push_back({ curStart, static_cast<int>(out.size()) + 1,
                                { Instruction{ OpCode::LOAD_SLOT, s, loc } } });
                            out.push_back(ins);
                            ++eliminated;
                            vstack.push_back(Val{ key, curStart, rs, true });
                        } else {
                            out.push_back(ins);
                            const int p = static_cast<int>(out.size()) - 1;
                            avail[key] = Avail{ -1, p, false, rs };
                            vstack.push_back(Val{ key, curStart, rs, false });
                        }
                    }
                    continue;
                }

                if (isPureUnary(op)) {
                    Val arg = pop();

                    if (arg.key.empty()) {
                        out.push_back(ins);
                        vstack.push_back(Val{});
                        continue;
                    }

                    std::string key = opName(op) + "(" + arg.key + ")";
                    const int curStart = arg.start;

                    auto it = avail.find(key);
                    if (it != avail.end() && it->second.spilled && arg.atom) {
                        edits.push_back({ curStart, static_cast<int>(out.size()) + 1,
                            { Instruction{ OpCode::LOAD_SLOT, it->second.slot, loc } } });
                        out.push_back(ins);
                        ++eliminated;
                        vstack.push_back(Val{ key, curStart, arg.readSlots, true });
                    } else if (it != avail.end() && arg.atom) {
                        const int s = nextSlot++;
                        it->second.slot = s;
                        it->second.spilled = true;
                        edits.push_back({ it->second.firstOpPos + 1, -1,
                            { Instruction{ OpCode::DUP_STORE_SLOT, s, mChunk.code[it->second.firstOpPos].loc } } });
                        edits.push_back({ curStart, static_cast<int>(out.size()) + 1,
                            { Instruction{ OpCode::LOAD_SLOT, s, loc } } });
                        out.push_back(ins);
                        ++eliminated;
                        vstack.push_back(Val{ key, curStart, arg.readSlots, true });
                    } else {
                        out.push_back(ins);
                        const int p = static_cast<int>(out.size()) - 1;
                        avail[key] = Avail{ -1, p, false, arg.readSlots };
                        vstack.push_back(Val{ key, curStart, arg.readSlots, false });
                    }
                    continue;
                }

                out.push_back(ins);
                vstack.clear();
                if (isBarrier(op))
                    avail.clear();
            }
        }

        apply(out, edits);
        mChunk.code = std::move(out);
        mChunk.slotCount = nextSlot;

        return eliminated;
    }
}
