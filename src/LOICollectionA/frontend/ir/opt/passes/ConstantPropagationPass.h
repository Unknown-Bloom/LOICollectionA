#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    class ConstantPropagationPass {
    public:
        explicit ConstantPropagationPass(MirChunk& chunk)
        : mChunk(chunk) {}

        size_t run();

    private:
        struct State {
            std::unordered_map<int, ValueNode::ValueType> regs;
            std::unordered_map<int, ValueNode::ValueType> slots;
            std::unordered_map<std::string, ValueNode::ValueType> names;
        };

        struct BlockState {
            State in;
            State out;
        };

        void transfer(int begin, int end, State& state) const;
        void step(const MirInstr& instr, State& state) const;

        State merge(const std::vector<const State*>& inputs) const;

        MirChunk& mChunk;
    };
}
