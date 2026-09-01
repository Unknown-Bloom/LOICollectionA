#pragma once

#include <string>
#include <variant>
#include <vector>

#include "LOICollectionA/frontend/ir/ByteCode.h"
#include "LOICollectionA/frontend/ir/OpCode.h"

#include "LOICollectionA/frontend/ir/opt/OptContext.h"

namespace LOICollection::frontend::ir::opt {
    inline bool isKnown(const StackEntry& entry) {
        return std::holds_alternative<TrackedValue>(entry);
    }

    inline const TrackedValue& knownValue(const StackEntry& entry) {
        return std::get<TrackedValue>(entry);
    }

    inline bool isScalarValue(const ValueNode::ValueType& value) {
        return !std::holds_alternative<ArrayRef>(value) &&
            !std::holds_alternative<ObjectRef>(value) &&
            !std::holds_alternative<FunctionRefPtr>(value);
    }

    StackEntry popEntry(std::vector<StackEntry>& stack);

    bool sameScalar(const ValueNode::ValueType& a, const ValueNode::ValueType& b);

    bool identityEligible(OpCode op, const StackEntry& kept, const StackEntry& identity);

    bool foldPureMath(const std::string& name, const std::vector<ValueNode::ValueType>& args, ValueNode::ValueType& out);

    int addConstant(BytecodeChunk& chunk, const ValueNode::ValueType& value);

    void emitPush(
        BytecodeChunk& chunk,
        std::vector<Instruction>& out,
        const ValueNode::ValueType& value,
        const SourceLocation& loc = {}
    );
}
