#pragma once

namespace LOICollection::frontend {
    struct ASTNode;
    struct ExprNode;
    struct ValueNode;
    struct VariableNode;
    struct AssignmentNode;
    struct CompoundAssignNode;
    struct IfNode;
    struct WhileNode;
    struct ForNode;
    struct ForInNode;
    struct RangeNode;
    struct CoalesceNode;
    struct BreakNode;
    struct ContinueNode;
    struct CompareNode;
    struct LogicalNode;
    struct FunctionNode;
    struct MacroNode;
    struct ArithmeticNode;
    struct UnaryNode;
    struct ProgramNode;
    struct BlockNode;
    struct ClassNode;
    struct ReturnNode;
    struct NewNode;
    struct MemberAccessNode;
    struct MethodCallNode;
    struct ThisNode;
    struct SuperNode;
    struct SuperCallNode;
    struct InstanceOfNode;
    struct FunctionDefNode;
    struct FuncCallNode;
    struct LambdaNode;
    struct ArrayNode;
    struct IndexAccessNode;
    struct UsingNode;

    class ASTVisitor {
    public:
        virtual ~ASTVisitor() = default;

        virtual void visit(ValueNode& node) = 0;
        virtual void visit(VariableNode& node) = 0;
        virtual void visit(AssignmentNode& node) = 0;
        virtual void visit(CompoundAssignNode& node) = 0;
        virtual void visit(IfNode& node) = 0;
        virtual void visit(WhileNode& node) = 0;
        virtual void visit(ForNode& node) = 0;
        virtual void visit(ForInNode& node) = 0;
        virtual void visit(RangeNode& node) = 0;
        virtual void visit(CoalesceNode& node) = 0;
        virtual void visit(BreakNode& node) = 0;
        virtual void visit(ContinueNode& node) = 0;
        virtual void visit(CompareNode& node) = 0;
        virtual void visit(LogicalNode& node) = 0;
        virtual void visit(FunctionNode& node) = 0;
        virtual void visit(MacroNode& node) = 0;
        virtual void visit(ArithmeticNode& node) = 0;
        virtual void visit(UnaryNode& node) = 0;
        virtual void visit(ProgramNode& node) = 0;
        virtual void visit(BlockNode& node) = 0;
        virtual void visit(ClassNode& node) = 0;
        virtual void visit(ReturnNode& node) = 0;
        virtual void visit(NewNode& node) = 0;
        virtual void visit(MemberAccessNode& node) = 0;
        virtual void visit(MethodCallNode& node) = 0;
        virtual void visit(ThisNode& node) = 0;
        virtual void visit(SuperNode& node) = 0;
        virtual void visit(SuperCallNode& node) = 0;
        virtual void visit(InstanceOfNode& node) = 0;
        virtual void visit(FunctionDefNode& node) = 0;
        virtual void visit(FuncCallNode& node) = 0;
        virtual void visit(LambdaNode& node) = 0;
        virtual void visit(ArrayNode& node) = 0;
        virtual void visit(IndexAccessNode& node) = 0;
        virtual void visit(UsingNode& node) = 0;
    };
}
