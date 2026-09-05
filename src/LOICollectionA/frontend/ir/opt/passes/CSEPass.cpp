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

        bool isBarrier(MirOp op) {
            return canWriteVariables(op)
                || op == MirOp::LOAD_FIELD
                || op == MirOp::LOAD_INDEX
                || op == MirOp::STORE_INDEX
                || op == MirOp::MAKE_ARRAY
                || op == MirOp::MAKE_LAMBDA;
        }

        bool isPureBinary(MirOp op) {
            switch (op) {
                case MirOp::ADD:
                case MirOp::SUB:
                case MirOp::MUL:
                case MirOp::DIV:
                case MirOp::MOD:
                case MirOp::POW:
                case MirOp::CMP_EQ:
                case MirOp::CMP_NE:
                case MirOp::CMP_GT:
                case MirOp::CMP_LT:
                case MirOp::CMP_GE:
                case MirOp::CMP_LE:
                    return true;
                default:
                    return false;
            }
        }

        bool isPureUnary(MirOp op) {
            switch (op) {
                case MirOp::NEG:
                case MirOp::NOT:
                case MirOp::INSTANCEOF:
                case MirOp::LOAD_LEN:
                case MirOp::DUP_IS_NONE:
                    return true;
                default:
                    return false;
            }
        }

        bool isLeaf(MirOp op) {
            switch (op) {
                case MirOp::LOAD_SLOT:
                case MirOp::LOAD_VAR:
                case MirOp::LOAD_THIS:
                case MirOp::PUSH_INT:
                case MirOp::PUSH_FLOAT:
                case MirOp::PUSH_STR:
                case MirOp::PUSH_BOOL:
                case MirOp::PUSH_NONE:
                    return true;
                default:
                    return false;
            }
        }

        std::string opName(MirOp op) {
            switch (op) {
                case MirOp::ADD: return "ADD";
                case MirOp::SUB: return "SUB";
                case MirOp::MUL: return "MUL";
                case MirOp::DIV: return "DIV";
                case MirOp::MOD: return "MOD";
                case MirOp::POW: return "POW";
                case MirOp::CMP_EQ: return "EQ";
                case MirOp::CMP_NE: return "NE";
                case MirOp::CMP_GT: return "GT";
                case MirOp::CMP_LT: return "LT";
                case MirOp::CMP_GE: return "GE";
                case MirOp::CMP_LE: return "LE";
                case MirOp::NEG: return "NEG";
                case MirOp::NOT: return "NOT";
                case MirOp::INSTANCEOF: return "INST";
                case MirOp::LOAD_LEN: return "LEN";
                case MirOp::DUP_IS_NONE: return "ISNONE";
                default: return "?";
            }
        }

        std::string constKey(MirOp op, int operand) {
            switch (op) {
                case MirOp::PUSH_INT: return "I:" + std::to_string(operand);
                case MirOp::PUSH_FLOAT: return "F:" + std::to_string(operand);
                case MirOp::PUSH_STR: return "S:" + std::to_string(operand);
                case MirOp::PUSH_BOOL: return "B:" + std::to_string(operand);
                case MirOp::PUSH_NONE: return "N";
                default: return "?";
            }
        }

        void apply(std::vector<MirInstr>& code, std::vector<CSEPass::Edit>& edits) {
            std::sort(edits.begin(), edits.end(),
                [](const CSEPass::Edit& a, const CSEPass::Edit& b) { return a.at < b.at; });

            const int n = static_cast<int>(code.size());

            std::vector<MirInstr> rewritten;
            rewritten.reserve(code.size());

            std::vector<int> oldToNew(n, -1);
            std::vector<int> origin;
            std::vector<std::vector<int>> placed(edits.size());

            for (int pos = 0; pos <= n; ++pos) {
                bool erased = false;
                int redirect = -1;

                for (size_t e = 0; e < edits.size(); ++e) {
                    if (edits[e].at == pos) {
                        for (const MirInstr& ins : edits[e].insert) {
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
        std::vector<MirInstr> out;
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
                const MirInstr& ins = mChunk.code[i];
                const MirOp op = ins.op;
                const SourceLocation loc = ins.loc;

                if (isLeaf(op)) {
                    std::string key;
                    std::vector<int> rs;

                    if (op == MirOp::LOAD_SLOT || op == MirOp::LOAD_VAR) {
                        key = (op == MirOp::LOAD_SLOT ? "L" : "V") + std::to_string(ins.operand);
                        rs.push_back(ins.operand);
                    } else if (op == MirOp::LOAD_THIS) {
                        key = "THIS";
                    } else {
                        key = constKey(op, ins.operand);
                    }

                    const int start = static_cast<int>(out.size());
                    out.push_back(ins);
                    vstack.push_back(Val{ key, start, rs, true });
                    continue;
                }

                if (op == MirOp::DUP) {
                    if (!vstack.empty())
                        vstack.push_back(vstack.back());
                    out.push_back(ins);
                    continue;
                }

                if (op == MirOp::POP) {
                    if (!vstack.empty())
                        vstack.pop_back();
                    out.push_back(ins);
                    continue;
                }

                if (op == MirOp::STORE_SLOT || op == MirOp::STORE_VAR) {
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
                            { MirInstr{ MirOp::LOAD_SLOT, it->second.slot, loc } } });
                        out.push_back(ins);
                        ++eliminated;
                        vstack.push_back(Val{ key, curStart, rs, true });
                    } else {
                        if (it != avail.end() && left.atom && right.atom) {
                            const int s = nextSlot++;
                            it->second.slot = s;
                            it->second.spilled = true;
                            edits.push_back({ it->second.firstOpPos + 1, -1,
                                { MirInstr{ MirOp::DUP_STORE_SLOT, s, mChunk.code[it->second.firstOpPos].loc } } });
                            edits.push_back({ curStart, static_cast<int>(out.size()) + 1,
                                { MirInstr{ MirOp::LOAD_SLOT, s, loc } } });
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
                            { MirInstr{ MirOp::LOAD_SLOT, it->second.slot, loc } } });
                        out.push_back(ins);
                        ++eliminated;
                        vstack.push_back(Val{ key, curStart, arg.readSlots, true });
                    } else if (it != avail.end() && arg.atom) {
                        const int s = nextSlot++;
                        it->second.slot = s;
                        it->second.spilled = true;
                        edits.push_back({ it->second.firstOpPos + 1, -1,
                            { MirInstr{ MirOp::DUP_STORE_SLOT, s, mChunk.code[it->second.firstOpPos].loc } } });
                        edits.push_back({ curStart, static_cast<int>(out.size()) + 1,
                            { MirInstr{ MirOp::LOAD_SLOT, s, loc } } });
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
