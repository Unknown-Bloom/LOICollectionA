#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"
#include "LOICollectionA/frontend/ir/opt/analysis/ControlFlowGraph.h"

#include "LOICollectionA/frontend/ir/opt/passes/CSEPass.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        bool isLeaf(MirOp op) {
            switch (op) {
                case MirOp::LOAD_CONST:
                case MirOp::LOAD_SLOT:
                case MirOp::LOAD_VAR:
                case MirOp::LOAD_THIS:
                    return true;
                default:
                    return false;
            }
        }

        bool isPureBinary(MirOp op) {
            switch (op) {
                case MirOp::ADD: case MirOp::SUB: case MirOp::MUL: case MirOp::DIV:
                case MirOp::MOD: case MirOp::POW:
                case MirOp::CMP_EQ: case MirOp::CMP_NE: case MirOp::CMP_GT: case MirOp::CMP_LT:
                case MirOp::CMP_GE: case MirOp::CMP_LE:
                case MirOp::LOGIC_AND: case MirOp::LOGIC_OR:
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
                    return true;
                default:
                    return false;
            }
        }

        bool isBarrier(MirOp op) {
            return canWriteVariables(op)
                || op == MirOp::LOAD_FIELD
                || op == MirOp::LOAD_FIELD_SLOT
                || op == MirOp::LOAD_INDEX
                || op == MirOp::STORE_INDEX
                || op == MirOp::MAKE_ARRAY
                || op == MirOp::MAKE_LAMBDA
                || op == MirOp::NEW
                || op == MirOp::NEW_NATIVE
                || op == MirOp::STORE_FIELD_SLOT;
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
                case MirOp::LOGIC_AND: return "AND";
                case MirOp::LOGIC_OR: return "OR";
                case MirOp::NEG: return "NEG";
                case MirOp::NOT: return "NOT";
                case MirOp::INSTANCEOF: return "INST";
                case MirOp::LOAD_LEN: return "LEN";
                default: return "?";
            }
        }

        std::string leafKey(const MirInstr& ins, const MirChunk& chunk) {
            switch (ins.op) {
                case MirOp::LOAD_CONST: return "K:" + std::to_string(ins.operand);
                case MirOp::LOAD_SLOT: return "L:" + std::to_string(ins.operand);
                case MirOp::LOAD_VAR: return "V:" + std::get<std::string>(chunk.constants[ins.operand]);
                case MirOp::LOAD_THIS: return "THIS";
                default: return "?";
            }
        }
    }

    size_t CSEPass::run() {
        const ControlFlowGraph cfg{ mChunk.code };

        std::vector<MirInstr> out;
        out.reserve(mChunk.code.size());
        std::vector<int> oldToNew(mChunk.code.size(), -1);
        std::vector<int> origin;
        size_t eliminated = 0;

        for (const ControlFlowGraph::Block& block : cfg.blocks()) {
            std::unordered_map<int, std::string> regKey;
            std::unordered_map<std::string, int> keyReg;

            for (int i = block.begin; i < block.end; ++i) {
                const MirInstr& ins = mChunk.code[i];
                const MirOp op = ins.op;

                if (ins.dst >= 0) {
                    for (auto it = keyReg.begin(); it != keyReg.end();) {
                        if (it->second == ins.dst)
                            it = keyReg.erase(it);
                        else
                            ++it;
                    }
                }

                if (isBarrier(op)) {
                    regKey.clear();
                    keyReg.clear();
                    out.push_back(ins);
                    oldToNew[i] = static_cast<int>(out.size()) - 1;
                    origin.push_back(i);
                    continue;
                }

                if (isLeaf(op)) {
                    const std::string key = leafKey(ins, mChunk);
                    out.push_back(ins);
                    oldToNew[i] = static_cast<int>(out.size()) - 1;
                    origin.push_back(i);
                    regKey[ins.dst] = key;
                    keyReg[key] = ins.dst;
                    continue;
                }

                if (op == MirOp::MOVE) {
                    out.push_back(ins);
                    oldToNew[i] = static_cast<int>(out.size()) - 1;
                    origin.push_back(i);
                    if (auto it = regKey.find(ins.src1); it != regKey.end())
                        regKey[ins.dst] = it->second;
                    else
                        regKey.erase(ins.dst);
                    continue;
                }

                const bool binary = isPureBinary(op);
                const bool unary = isPureUnary(op);

                if (binary || unary) {
                    std::string key;
                    if (binary) {
                        auto l = regKey.find(ins.src1);
                        auto r = regKey.find(ins.src2);
                        if (l != regKey.end() && r != regKey.end())
                            key = opName(op) + "(" + l->second + "," + r->second + ")";
                    } else {
                        auto a = regKey.find(ins.src1);
                        if (a != regKey.end())
                            key = opName(op) + "(" + a->second + ")";
                    }

                    int existing = -1;
                    if (!key.empty()) {
                        auto it = keyReg.find(key);
                        if (it != keyReg.end() && it->second != ins.dst)
                            existing = it->second;
                    }

                    if (existing >= 0) {
                        out.push_back({ MirOp::MOVE, 0, ins.dst, existing, -1, -1, 0, {}, ins.loc });
                        oldToNew[i] = static_cast<int>(out.size()) - 1;
                        origin.push_back(i);
                        regKey[ins.dst] = key;
                        keyReg[key] = ins.dst;
                        ++eliminated;
                    } else {
                        out.push_back(ins);
                        oldToNew[i] = static_cast<int>(out.size()) - 1;
                        origin.push_back(i);
                        if (!key.empty()) {
                            regKey[ins.dst] = key;
                            keyReg[key] = ins.dst;
                        } else {
                            regKey.erase(ins.dst);
                        }
                    }
                    continue;
                }

                out.push_back(ins);
                oldToNew[i] = static_cast<int>(out.size()) - 1;
                origin.push_back(i);
                if (ins.dst >= 0)
                    regKey.erase(ins.dst);
            }
        }

        for (size_t k = 0; k < out.size(); ++k) {
            if (!isJump(out[k].op))
                continue;

            const int oldTarget = origin[k] + 1 + out[k].operand;

            int newTarget = oldToNew[oldTarget];
            for (int j = oldTarget; j < static_cast<int>(oldToNew.size()) && newTarget < 0; ++j)
                newTarget = oldToNew[j];

            if (newTarget < 0)
                newTarget = static_cast<int>(out.size());

            out[k].operand = newTarget - static_cast<int>(k) - 1;
        }

        mChunk.code = std::move(out);

        return eliminated;
    }
}
