#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

namespace LOICollection::frontend::ir::opt {
    std::string arithmeticOpName(OpCode op) {
        switch (op) {
            case OpCode::ADD: return "+";
            case OpCode::SUB: return "-";
            case OpCode::MUL: return "*";
            case OpCode::DIV: return "/";
            case OpCode::MOD: return "%";
            case OpCode::POW: return "^";
            default: return "";
        }
    }

    std::string comparisonOpName(OpCode op) {
        switch (op) {
            case OpCode::CMP_EQ: return "==";
            case OpCode::CMP_NE: return "!=";
            case OpCode::CMP_GT: return ">";
            case OpCode::CMP_LT: return "<";
            case OpCode::CMP_GE: return ">=";
            case OpCode::CMP_LE: return "<=";
            default: return "";
        }
    }
}
