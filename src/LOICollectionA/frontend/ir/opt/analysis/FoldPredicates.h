#pragma once

#include <string>
#include <variant>
#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    inline bool isScalarValue(const ValueNode::ValueType& value) {
        return !std::holds_alternative<ArrayRef>(value) &&
            !std::holds_alternative<ObjectRef>(value) &&
            !std::holds_alternative<FunctionRefPtr>(value);
    }

    bool sameScalar(const ValueNode::ValueType& a, const ValueNode::ValueType& b);

    bool foldPureMath(const std::string& name, const std::vector<ValueNode::ValueType>& args, ValueNode::ValueType& out);

    int addConstant(MirChunk& chunk, const ValueNode::ValueType& value);

    int emitLoadConst(
        MirChunk& chunk,
        std::vector<MirInstr>& out,
        const ValueNode::ValueType& value,
        const SourceLocation& loc = {}
    );
}
