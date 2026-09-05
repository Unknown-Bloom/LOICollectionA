#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/base/ScopeGuard.h"

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Iteration.h"

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir {
    // Three-address register compiler.
    //
    // Every expression result lands in a virtual register; `compileValue`
    // returns the register holding the value. Registers are bump-allocated
    // from the enclosing scope, which means a slot index doubles as a
    // register number and the VM's `localPool` is the register file.
    //
    // `dstHint` lets a caller request that an expression be materialised
    // directly into a specific register (argument window slots, phi merge
    // registers). It is advisory: `finishHint` repairs the placement with a
    // MOVE when the expression could not honour it.
    class Compiler : public ASTVisitor {
    public:
        LOICOLLECTION_A_API   Compiler(DiagnosticEngine& diag);

        LOICOLLECTION_A_NDAPI MirChunk compile(ASTNode& root);

    private:
        MirChunk chunk;
        
        std::reference_wrapper<MirChunk> current;

        DiagnosticEngine& diagnostics;

        std::unordered_map<std::string, int> classIndices;
        std::unordered_map<std::string, std::reference_wrapper<ClassNode>> classNodes;
        std::unordered_set<int> registeredClasses;
        std::unordered_set<std::string> registeringClasses;
        std::unordered_map<std::string, std::vector<int>> classMethodIndices;
        std::unordered_map<std::string, std::vector<int>> classStaticMethodIndices;
        std::unordered_map<std::string, std::vector<int>> functionIndices;
        std::vector<std::reference_wrapper<ASTNode>> bodyOrder;

        struct LoopContext {
            std::vector<size_t> breakJumps;
            std::vector<size_t> continueJumps;
            size_t continueTarget = 0;
        };

        std::vector<LoopContext> loopStack;

        struct Scope {
            std::unordered_map<std::string, int> slots;

            int base = 0;
            int next = 0;
            bool hasThis = false;
            bool inherits = false;
        };

        std::vector<Scope> scopes;

        size_t methodCount = 0;
        size_t forInCounter = 0;
        size_t declarativeCounter = 0;

        int lastResultReg = -1;
        int dstHint = -1;

        void visit(ValueNode& node) override;
        void visit(VariableNode& node) override;
        void visit(AssignmentNode& node) override;
        void visit(CompoundAssignNode& node) override;
        void visit(IfNode& node) override;
        void visit(WhileNode& node) override;
        void visit(ForNode& node) override;
        void visit(ForInNode& node) override;
        void visit(RangeNode& node) override;
        void visit(CoalesceNode& node) override;
        void visit(BreakNode& node) override;
        void visit(ContinueNode& node) override;
        void visit(CompareNode& node) override;
        void visit(LogicalNode& node) override;
        void visit(FunctionNode& node) override;
        void visit(MacroNode& node) override;
        void visit(ArithmeticNode& node) override;
        void visit(UnaryNode& node) override;
        void visit(ProgramNode& node) override;
        void visit(BlockNode& node) override;
        void visit(ClassNode& node) override;
        void visit(ReturnNode& node) override;
        void visit(NewNode& node) override;
        void visit(MemberAccessNode& node) override;
        void visit(MethodCallNode& node) override;
        void visit(ThisNode& node) override;
        void visit(SuperNode& node) override;
        void visit(SuperCallNode& node) override;
        void visit(InstanceOfNode& node) override;
        void visit(FunctionDefNode& node) override;
        void visit(FuncCallNode& node) override;
        void visit(LambdaNode& node) override;
        void visit(ArrayNode& node) override;
        void visit(IndexAccessNode& node) override;
        void visit(UsingNode& node) override;
        void visit(ImportNode& node) override;
        void visit(ComponentNode& node) override;

        void registerClassMeta(ClassNode& node);
        void compileClassBodies(ClassNode& node);
        void registerFunctionMeta(FunctionDefNode& node);
        void compileFunctionBody(FunctionDefNode& node);
        void compileSequence(SequenceNode& node);

        // Register allocation.
        int allocReg() { return this->scopes.back().next++; }

        int reserveRegs(int count) {
            const int base = this->scopes.back().next;
            this->scopes.back().next += count;
            return base;
        }

        int takeDst() {
            return this->dstHint >= 0 ? std::exchange(this->dstHint, -1) : this->allocReg();
        }

        [[nodiscard]] int compilePart(ASTNode& node);

        int compileValue(ExprNode& node, const SourceLocation& loc);
        int compileNone(const SourceLocation& loc);
        int compileInto(int hint, ASTNode& node, const SourceLocation& loc);
        int compileArg(int hint, ExprNode& node, const SourceLocation& loc);
        int finishHint(int hint, int reg, const SourceLocation& loc);

        [[nodiscard]] std::optional<int> tryEmitLeaf(ASTNode& node, int dst, const SourceLocation& loc);

        int emitBoolConst(bool value, const SourceLocation& loc);

        int emitBinary(
            MirOp genericOp, MirOp intOp, bool isInt,
            int lhs, int rhs,
            const SourceLocation& loc, const TypeInfo& type = {}
        );

        [[nodiscard]] ClassLookup classLookup() const;

        void compileForInCounter(ForInNode& node, size_t uid);
        void compileForInIterable(ForInNode& node, size_t uid, const IterableProtocol& protocol);
        int emitIterableLength(const IterableProtocol& protocol, int seqSlot, const SourceLocation& loc);
        int emitIterableElement(const IterableProtocol& protocol, int seqSlot, int idxSlot, const SourceLocation& loc);
        int emitArithmeticOp(
            const std::string& op, const TypeInfo& leftType, const TypeInfo& rightType,
            int lhs, int rhs, const SourceLocation& loc
        );

        void desugarDeclarativeStatements(std::unique_ptr<ASTNode>& node, const std::string& receiver);
        void compileDeclarativeBlock(BlockNode& block, const std::string& receiverName);

        [[nodiscard]] std::optional<ValueNode::ValueType> constantValue(ExprNode& node) const;

        int addNativeCall(const std::string& className, const std::string& name, int argCount, bool isStatic = false);
        int addConstant(const ValueNode::ValueType& val);
        int addFunction(const std::string& name, int argCount);
        int addMacro(const std::string& name, int argCount);
        int addLambda(int bodyIndex, int argCount, int captureCount);
        int addVirtualCall(int classIndex, int ordinal, int argCount);
        int addByNameCall(const std::string& methodName, int argCount);
        int addSuperCall(int constructorIndex, int argCount);

        [[nodiscard]] std::string methodSignature(const MethodDecl& method) const;

        [[nodiscard]] int fieldSlotOf(const TypeInfo& owner, const std::string& memberName) const;

        [[nodiscard]] auto suspendLoops() {
            auto saved = std::exchange(this->loopStack, {});
            return make_scope_guard([this, saved = std::move(saved)]() mutable {
                this->loopStack = std::move(saved);
            });
        }

        void pushScope(bool hasThis, int base, bool inherits = false);
        int closeScope();
        int declareSlot(const std::string& name);
        [[nodiscard]] std::optional<int> resolveSlot(const std::string& name) const;

        void predeclareLocals(ASTNode& body);

        int emitLoad(const std::string& name, const SourceLocation& loc);
        int emitLoadInto(int dst, const std::string& name, const SourceLocation& loc);
        void emitStore(const std::string& name, int srcReg, const SourceLocation& loc);

        int emitLoadField(const MemberAccessNode& node);
        void emitLoadFieldInto(int dst, int objReg, const MemberAccessNode& node);
        void emitStoreField(int objReg, int valReg, const MemberAccessNode& node);
    };
}
