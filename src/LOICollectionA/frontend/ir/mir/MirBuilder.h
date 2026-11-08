#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/frontend/ir/hir/Hir.h"
#include "LOICollectionA/frontend/ir/mir/Mir.h"

namespace LOICollection::frontend::ir::mir {
    class MirBuilder {
    public:
        LOICOLLECTION_A_API MirBuilder(const hir::Program& program, DiagnosticEngine& diag);

        LOICOLLECTION_A_NDAPI Module build();

    private:
        struct LoopContext {
            BlockId breakTarget = invalidId;
            BlockId continueTarget = invalidId;
        };

        const hir::Program& program;
        DiagnosticEngine& diagnostics;
        Module module;
        Body* body = nullptr;
        const hir::Body* hirBody = nullptr;
        BlockId currentBlock = 0;
        bool finished = false;
        std::vector<LoopContext> loops;

        [[nodiscard]] BlockId newBlock();
        void selectBlock(BlockId block);
        void terminate(Terminator terminator);
        void emit(Statement statement);
        [[nodiscard]] ExprId addExpr(hir::Expr expr);
        [[nodiscard]] LocalId addTemp(ty::Ty type);
        [[nodiscard]] ExprId literal(const ValueNode::ValueType& value, ty::Ty type, const SourceLocation& loc);
        [[nodiscard]] ExprId localRef(LocalId local, const SourceLocation& loc);
        [[nodiscard]] ExprId preservedLocal(LocalId local, const SourceLocation& loc);
        [[nodiscard]] ExprId globalRef(const std::string& name, const SourceLocation& loc);
        [[nodiscard]] ExprId defaultExpr(const SourceLocation& loc);
        [[nodiscard]] ExprId noneExpr(const SourceLocation& loc);
        [[nodiscard]] const hir::Expr& expr(ExprId id) const;

        [[nodiscard]] ExprId makeIsNone(ExprId value, const SourceLocation& loc);
        [[nodiscard]] ExprId makeUnwrap(ExprId value, ty::Ty type, const SourceLocation& loc);
        [[nodiscard]] ExprId makeLen(ExprId value, const SourceLocation& loc);
        [[nodiscard]] ExprId makeIndex(ExprId target, ExprId index, ty::Ty type, const SourceLocation& loc);
        [[nodiscard]] ExprId makeField(ExprId target, const std::string& name, int slot, ty::Ty type, const SourceLocation& loc);
        [[nodiscard]] ExprId makeBinary(const std::string& op, ExprId left, ExprId right, ty::Ty type, const SourceLocation& loc);
        [[nodiscard]] ExprId makeCompare(const std::string& op, ExprId left, ExprId right, const SourceLocation& loc);
        [[nodiscard]] ExprId makeInstanceOf(ExprId value, const std::string& className, const SourceLocation& loc);
        [[nodiscard]] ExprId makePayload(ExprId value, int slot, ty::Ty type, const SourceLocation& loc);
        [[nodiscard]] ExprId makeNativeCall(const std::string& className, const std::string& name,
            std::vector<ExprId> args, bool isStatic, ty::Ty type, const SourceLocation& loc);
        [[nodiscard]] ExprId makeByNameCall(const std::string& name, std::vector<ExprId> args,
            ty::Ty type, const SourceLocation& loc);

        [[nodiscard]] bool isControl(hir::ExprKind kind, bool safe) const;
        [[nodiscard]] ExprId lowerExpr(hir::ExprId id);
        [[nodiscard]] ExprId lowerValue(hir::ExprId id);
        [[nodiscard]] ExprId lowerNew(hir::ExprId id);
        [[nodiscard]] ExprId lowerIf(hir::ExprId id);
        [[nodiscard]] ExprId lowerMatch(hir::ExprId id);
        [[nodiscard]] ExprId lowerCoalesce(hir::ExprId id);
        [[nodiscard]] ExprId lowerLogical(hir::ExprId id);
        [[nodiscard]] ExprId lowerTry(hir::ExprId id);
        [[nodiscard]] ExprId lowerSafe(hir::ExprId id);
        [[nodiscard]] ExprId lowerBlockValue(const std::vector<hir::StmtId>& statements, bool keepDeclaration);
        void lowerBlock(const std::vector<hir::StmtId>& statements);
        void lowerStmt(const hir::Stmt& statement);
        void lowerCondition(hir::ExprId condition, BlockId trueBlock, BlockId falseBlock);
        void lowerLetPattern(hir::ExprId condition, BlockId trueBlock, BlockId falseBlock);
        void lowerWhile(const hir::Stmt& statement);
        void lowerFor(const hir::Stmt& statement);
        void lowerForIn(const hir::Stmt& statement);
        [[nodiscard]] ExprId lowerStore(const hir::Stmt& statement);
        void buildBody(Body& target, const hir::Body& source);
    };
}
