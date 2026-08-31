#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "LOICollectionA/frontend/ir/ByteCode.h"

#include "LOICollectionA/frontend/ir/Optimizer.h"

namespace LOICollection::frontend::ir::opt {
    struct TrackedValue {
        ValueNode::ValueType value;
        int producer = -1;
        bool removable = false;
    };

    using StackEntry = std::variant<TrackedValue, std::monostate>;

    class OptContext {
    public:
        explicit OptContext(size_t codeSize)
        : oldToNew(codeSize, -1),
          dropped(codeSize, false) {}

        std::vector<Instruction> foldedCode;
        std::vector<int> newToOld;
        std::vector<int> oldToNew;
        std::vector<bool> dropped;
        std::vector<StackEntry> stack{ std::monostate{} };
        std::unordered_map<int, ValueNode::ValueType> slotValues;
        std::unordered_map<std::string, ValueNode::ValueType> nameValues;
        Optimizer::Stats stats;

        int emit(const Instruction& instr) {
            int at = static_cast<int>(foldedCode.size());
            foldedCode.push_back(instr);
            return at;
        }

        void drop(int producer) { dropped[producer] = true; }

        void resetStack() { stack.assign(1, std::monostate{}); }

        void clearTracked() {
            slotValues.clear();
            nameValues.clear();
        }

        void pinTop() {
            if (auto* known = std::get_if<TrackedValue>(&stack.back()))
                known->removable = false;
        }
    };
}
