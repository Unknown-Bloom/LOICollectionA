#pragma once

#include <string>
#include <variant>
#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    // Scalars survive constant propagation; aggregates (arrays, objects,
    // functions) are treated as opaque values.
    inline bool isScalarValue(const ValueNode::ValueType& value) {
        return !std::holds_alternative<ArrayRef>(value) &&
            !std::holds_alternative<ObjectRef>(value) &&
            !std::holds_alternative<FunctionRefPtr>(value);
    }

    bool sameScalar(const ValueNode::ValueType& a, const ValueNode::ValueType& b);

    // Folds a handful of pure host math functions (math::abs, math::min, ...)
    // when every argument is a known constant.
    bool foldPureMath(const std::string& name, const std::vector<ValueNode::ValueType>& args, ValueNode::ValueType& out);

    // Appends a constant to `chunk.constants`, returning its index (deduplicated
    // for scalars).
    int addConstant(MirChunk& chunk, const ValueNode::ValueType& value);

    // Emits a `LOAD_CONST` instruction for `value` into `out` and returns its index.
    int emitLoadConst(
        MirChunk& chunk,
        std::vector<MirInstr>& out,
        const ValueNode::ValueType& value,
        const SourceLocation& loc = {}
    );
}
