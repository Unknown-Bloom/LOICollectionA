#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "LOICollectionA/frontend/ir/hir/Hir.h"

namespace LOICollection::frontend::ir::mir {
    inline constexpr std::uint32_t invalidId = 0xFFFFFFFFu;

    using BlockId = std::uint32_t;
    using LocalId = hir::LocalId;
    using ExprId = std::uint32_t;

    enum class StatementKind : std::uint8_t {
        Evaluate,
        StoreLocal,
        StoreGlobal,
        StoreField,
        StoreIndex,
        CompoundAssign
    };

    struct Statement {
        StatementKind kind = StatementKind::Evaluate;
        SourceLocation loc{};
        ExprId expr = invalidId;
        ExprId target = invalidId;
        LocalId local = invalidId;
        std::string name;
        std::string op;
    };

    enum class TerminatorKind : std::uint8_t {
        Unreachable,
        Goto,
        Branch,
        SwitchTag,
        Return,
        Halt,
        Panic
    };

    struct Terminator {
        TerminatorKind kind = TerminatorKind::Unreachable;
        SourceLocation loc{};
        BlockId target = invalidId;
        BlockId alternate = invalidId;
        ExprId condition = invalidId;
        ExprId value = invalidId;
        int tagEnum = -1;
        int tag = -1;
    };

    struct Block {
        std::vector<Statement> statements;
        Terminator terminator;
    };

    struct Body {
        std::vector<hir::Local> locals;
        std::vector<hir::Expr> exprs;
        std::vector<Block> blocks;
        BlockId entry = 0;
        std::uint32_t paramCount = 0;
        bool isTopLevel = false;
        bool returnsValue = false;

        [[nodiscard]] bool validBlock(BlockId id) const { return id < this->blocks.size(); }
        [[nodiscard]] bool validLocal(LocalId id) const { return id < this->locals.size(); }
        [[nodiscard]] bool validExpr(ExprId id) const { return id < this->exprs.size(); }
    };

    struct Module {
        std::vector<Body> bodies;
    };
}
