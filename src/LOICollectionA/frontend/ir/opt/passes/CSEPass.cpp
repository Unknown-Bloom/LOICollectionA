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
                    return true;
                default:
                    return false;
            }
        }

        bool isPureUnary(OpCode op) {
            switch (op) {
                case OpCode::NEG:
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

        bool isStackReset(OpCode op) {
            switch (op) {
                case OpCode::ROT3:
                case OpCode::SWAP2:
                case OpCode::DUP2:
                case OpCode::UNWRAP:
                case OpCode::BIND_THIS:
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

            std::vector<Instruction> rewritten;
            rewritten.reserve(code.size());

            const int n = static_cast<int>(code.size());
            size_t ei = 0;

            for (int pos = 0; pos <= n; ++pos) {
                while (ei < edits.size() && edits[ei].at == pos) {
                    for (const Instruction& ins : edits[ei].insert)
                        rewritten.push_back(ins);
                    ++ei;
                }

                if (pos == n)
                    break;

                bool erased = false;
                for (const CSEPass::Edit& e : edits)
                    if (e.at <= pos && pos < e.eraseTo) {
                        erased = true;
                        break;
                    }

                if (!erased)
                    rewritten.push_back(code[pos]);
            }

            while (ei < edits.size()) {
                for (const Instruction& ins : edits[ei].insert)
                    rewritten.push_back(ins);
                ++ei;
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
                        edits.push_back({ curStart, static_cast<int>(out.size()),
                            { Instruction{ OpCode::LOAD_SLOT, it->second.slot, loc } } });
                        ++eliminated;
                        vstack.push_back(Val{ key, curStart, rs, true });
                    } else {
                        if (it != avail.end() && left.atom && right.atom) {
                            const int s = nextSlot++;
                            it->second.slot = s;
                            it->second.spilled = true;
                            edits.push_back({ it->second.firstOpPos + 1, -1,
                                { Instruction{ OpCode::DUP_STORE_SLOT, s, mChunk.code[it->second.firstOpPos].loc } } });
                            edits.push_back({ curStart, static_cast<int>(out.size()),
                                { Instruction{ OpCode::LOAD_SLOT, s, loc } } });
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
                        edits.push_back({ curStart, static_cast<int>(out.size()),
                            { Instruction{ OpCode::LOAD_SLOT, it->second.slot, loc } } });
                        ++eliminated;
                        vstack.push_back(Val{ key, curStart, arg.readSlots, true });
                    } else if (it != avail.end() && arg.atom) {
                        const int s = nextSlot++;
                        it->second.slot = s;
                        it->second.spilled = true;
                        edits.push_back({ it->second.firstOpPos + 1, -1,
                            { Instruction{ OpCode::DUP_STORE_SLOT, s, mChunk.code[it->second.firstOpPos].loc } } });
                        edits.push_back({ curStart, static_cast<int>(out.size()),
                            { Instruction{ OpCode::LOAD_SLOT, s, loc } } });
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
