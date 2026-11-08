#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"
#include "LOICollectionA/frontend/ir/opt/analysis/ControlFlowGraph.h"

#include "LOICollectionA/frontend/ir/opt/passes/CSEPass.h"

namespace LOICollection::frontend::ir::opt::detail {
    bool isLeaf(Op op) {
        switch (op) {
            case Op::LOAD_CONST:
            case Op::LOAD_SLOT:
            case Op::LOAD_VAR:
            case Op::LOAD_THIS:
                return true;
            default:
                return false;
        }
    }

    bool isBarrier(Op op) {
        return canWriteVariables(op)
            || op == Op::LOAD_FIELD
            || op == Op::LOAD_FIELD_SLOT
            || op == Op::LOAD_INDEX
            || op == Op::STORE_INDEX
            || op == Op::MAKE_ARRAY
            || op == Op::MAKE_LAMBDA
            || op == Op::NEW
            || op == Op::NEW_NATIVE
            || op == Op::STORE_FIELD_SLOT;
    }

    std::string opName(Op op) {
        switch (op) {
            case Op::ADD: return "ADD";
            case Op::SUB: return "SUB";
            case Op::MUL: return "MUL";
            case Op::DIV: return "DIV";
            case Op::MOD: return "MOD";
            case Op::POW: return "POW";
            case Op::CMP_EQ: return "EQ";
            case Op::CMP_NE: return "NE";
            case Op::CMP_GT: return "GT";
            case Op::CMP_LT: return "LT";
            case Op::CMP_GE: return "GE";
            case Op::CMP_LE: return "LE";
            case Op::LOGIC_AND: return "AND";
            case Op::LOGIC_OR: return "OR";
            case Op::NEG: return "NEG";
            case Op::NOT: return "NOT";
            case Op::INSTANCEOF: return "INST";
            case Op::LOAD_LEN: return "LEN";
            default: return "?";
        }
    }

    std::string leafKey(const Instr& ins, const Chunk& chunk) {
        switch (ins.op) {
            case Op::LOAD_CONST: return "K:" + std::to_string(ins.operand);
            case Op::LOAD_SLOT: return "L:" + std::to_string(ins.operand);
            case Op::LOAD_VAR: return "V:" + std::get<std::string>(chunk.constants[ins.operand]);
            case Op::LOAD_THIS: return "THIS";
            default: return "?";
        }
    }
}

namespace LOICollection::frontend::ir::opt {
    using namespace detail;

    size_t CSEPass::run() {
        const ControlFlowGraph cfg{ mChunk.code };

        std::vector<Instr> out;
        out.reserve(mChunk.code.size());
        std::vector<int> oldToNew(mChunk.code.size(), -1);
        std::vector<int> origin;
        size_t eliminated = 0;

        for (const ControlFlowGraph::Block& block : cfg.blocks()) {
            std::unordered_map<int, std::string> regKey;
            std::unordered_map<std::string, int> keyReg;

            for (int i = block.begin; i < block.end; ++i) {
                const Instr& ins = mChunk.code[i];
                const Op op = ins.op;

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

                if (op == Op::MOVE) {
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
                            key = opName(op) + "#" + std::to_string(ins.operand) + "(" + a->second + ")";
                    }

                    int existing = -1;
                    if (!key.empty()) {
                        auto it = keyReg.find(key);
                        if (it != keyReg.end() && it->second != ins.dst)
                            existing = it->second;
                    }

                    if (existing >= 0) {
                        out.push_back({ Op::MOVE, 0, ins.dst, existing, -1, -1, 0, {}, ins.loc });
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
