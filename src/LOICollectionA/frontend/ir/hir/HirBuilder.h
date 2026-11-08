#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Iteration.h"

#include "LOICollectionA/frontend/ir/hir/Hir.h"
#include "LOICollectionA/frontend/ir/ty/TypeContext.h"

namespace LOICollection::frontend::ir::hir {
    class HirBuilder {
    public:
        LOICOLLECTION_A_API HirBuilder(DiagnosticEngine& diag);

        LOICOLLECTION_A_NDAPI Program build(ASTNode& root);

    private:
        struct Scope {
            std::unordered_map<std::string, LocalId> locals;
            std::unordered_map<std::string, ValueNode::ValueType> constants;
            Body* body = nullptr;
            bool inherits = false;
            bool hasThis = false;
        };

        DiagnosticEngine& diagnostics;
        Program program;
        Body* current = nullptr;
        std::vector<Scope> scopes;
        std::vector<std::vector<Capture>> captureStack;
        std::unordered_map<std::string, std::reference_wrapper<ClassNode>> classNodes;
        std::unordered_set<std::string> registeringClasses;
        std::unordered_map<std::string, std::vector<int>> classMethodIndices;
        std::unordered_set<const ASTNode*> hoistedConstants;
        std::vector<std::reference_wrapper<ASTNode>> bodyOrder;
        std::size_t methodCount = 0;
        std::size_t forInCounter = 0;
        std::size_t declarativeCounter = 0;
        std::vector<LocalId> declarativeReceivers;
        bool lastPartWasDeclaration = false;

        [[nodiscard]] std::string qualifiedName(const VariableNode& node) const;
        [[nodiscard]] std::string qualifiedName(const MemberAccessNode& node) const;

        void pushScope(bool hasThis, bool inherits);
        void popScope();
        LocalId declareLocal(const std::string& name, ty::Ty type, Ownership ownership, bool parameter = false);
        std::optional<LocalId> resolveLocal(const std::string& name) const;
        std::optional<LocalId> resolveOrCapture(const std::string& name);
        LocalId captureLocal(const std::string& name, const Scope& source);
        [[nodiscard]] std::optional<ValueNode::ValueType> resolveConstant(const std::string& name) const;
        void declareConstant(const std::string& name, const ValueNode::ValueType& value, const SourceLocation& loc);
        [[nodiscard]] std::optional<ValueNode::ValueType> constantValue(ExprNode& node) const;
        void collectTopLevelConstant(ASTNode& node);

        [[nodiscard]] ty::Ty lowerType(const TypeInfo& type) const;
        [[nodiscard]] int fieldSlotOf(const TypeInfo& owner, const std::string& name) const;
        [[nodiscard]] ExprId addExpr(Expr expr);
        [[nodiscard]] StmtId addStmt(Stmt stmt);
        [[nodiscard]] ExprId literalExpr(const ValueNode::ValueType& value, const SourceLocation& loc, ty::Ty type);
        [[nodiscard]] ExprId localRef(LocalId id, const SourceLocation& loc);
        [[nodiscard]] ExprId globalRef(const std::string& name, const SourceLocation& loc);
        [[nodiscard]] ExprId defaultExpr(const SourceLocation& loc);

        ExprId buildExpr(ExprNode& node);
        ExprId buildExprImpl(ExprNode& node);
        std::vector<StmtId> buildSequence(SequenceNode& node);
        std::vector<StmtId> buildBodyOf(ASTNode& node);
        StmtId buildStmt(ASTNode& node);
        StmtId buildAssignment(AssignmentNode& node);
        StmtId buildCompoundAssign(CompoundAssignNode& node);
        StmtId buildWhile(WhileNode& node);
        StmtId buildFor(ForNode& node);
        StmtId buildForIn(ForInNode& node);
        StmtId buildMatch(MatchNode& node);
        StmtId buildReturn(ReturnNode& node);
        StmtId buildPanic(PanicNode& node);
        StmtId buildBlock(BlockNode& node);

        ExprId buildValue(ValueNode& node);
        ExprId buildVariable(VariableNode& node);
        ExprId buildMemberAccess(MemberAccessNode& node);
        ExprId buildIndex(IndexAccessNode& node);
        ExprId buildFunction(FunctionNode& node);
        ExprId buildMacro(MacroNode& node);
        ExprId buildFuncCall(FuncCallNode& node);
        ExprId buildMethodCall(MethodCallNode& node);
        ExprId buildNew(NewNode& node);
        ExprId buildLambda(LambdaNode& node);
        ExprId buildArray(ArrayNode& node);
        ExprId buildArithmetic(ArithmeticNode& node);
        ExprId buildUnary(UnaryNode& node);
        ExprId buildCompare(CompareNode& node);
        ExprId buildLogical(LogicalNode& node);
        ExprId buildCoalesce(CoalesceNode& node);
        ExprId buildTry(TryNode& node);
        ExprId buildRange(RangeNode& node);
        ExprId buildIfExpr(IfNode& node);
        ExprId buildMatchExpr(MatchNode& node);
        ExprId buildLetPattern(LetPatternNode& node);

        void registerClassMeta(ClassNode& node);
        void registerFunctionMeta(FunctionDefNode& node);
        void registerEnumMeta(EnumDeclNode& node);
        void buildClassBodies(ClassNode& node);
        void buildFunctionBody(FunctionDefNode& node);
        void buildMethodBody(ClassNode& node, MethodDecl& method, int classIndex, int ordinal, std::uint32_t bodyIndex);
        [[nodiscard]] std::uint32_t appendBody();
        std::vector<StmtId> buildDeclarativeBlock(BlockNode& block, LocalId receiver);
    };
}
