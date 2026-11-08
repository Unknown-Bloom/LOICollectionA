#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/frontend/ir/hir/Hir.h"
#include "LOICollectionA/frontend/ir/mir/Mir.h"
#include "LOICollectionA/frontend/ir/Lir.h"

namespace LOICollection::frontend::ir {
    class LirLowering {
    public:
        LOICOLLECTION_A_API LirLowering(const hir::Program& program, const mir::Module& module, DiagnosticEngine& diag);

        LOICOLLECTION_A_NDAPI Chunk lower();

    private:
        struct JumpPatch {
            std::size_t index = 0;
            mir::BlockId target = 0;
        };

        const hir::Program& program;
        const mir::Module& module;
        DiagnosticEngine& diagnostics;
        Chunk root;
        Chunk* body = nullptr;
        int slotBase = 0;
        int nextReg = 0;
        std::vector<JumpPatch> jumps;
        std::vector<int> blockStarts;

        [[nodiscard]] int slotOf(const mir::Body& target, hir::LocalId local) const;
        [[nodiscard]] int allocReg();
        [[nodiscard]] int reserveRegs(int count);
        [[nodiscard]] int addConstant(const ValueNode::ValueType& value);
        [[nodiscard]] int addFunction(const std::string& name, int argCount);
        [[nodiscard]] int addMacro(const std::string& name, int argCount);
        [[nodiscard]] int addNativeCall(const std::string& className, const std::string& name, int argCount, bool isStatic);
        [[nodiscard]] int addVirtualCall(int classIndex, int ordinal, int argCount);
        [[nodiscard]] int addByNameCall(const std::string& name, int argCount);
        [[nodiscard]] int addSuperCall(int constructorIndex, int argCount);
        [[nodiscard]] int addLambda(int bodyIndex, int argCount, const std::vector<LambdaCapture>& captures);
        [[nodiscard]] TypeInfo legacyType(ty::Ty type) const;

        void buildMetadata();
        void lowerBody(const mir::Body& source, Chunk& target, bool topLevel);
        void lowerBlock(const mir::Body& source, const mir::Block& block);
        void lowerStatement(const mir::Body& source, const mir::Statement& statement);
        void lowerTerminator(const mir::Body& source, const mir::Terminator& terminator);

        [[nodiscard]] int compileValue(const mir::Body& source, mir::ExprId id);
        [[nodiscard]] int compileInto(const mir::Body& source, int dst, mir::ExprId id);
        [[nodiscard]] std::optional<int> tryEmitLeaf(const mir::Body& source, int dst, mir::ExprId id);
        [[nodiscard]] int compileArgs(const mir::Body& source, const std::vector<mir::ExprId>& args);
        [[nodiscard]] int compileOperand(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int emitBinary(Op op, int left, int right, const SourceLocation& loc, const TypeInfo& type = {});
        [[nodiscard]] int emitArithmetic(const std::string& op, const ty::Ty& left, const ty::Ty& right,
            int leftReg, int rightReg, const SourceLocation& loc);
        [[nodiscard]] int emitCompare(const std::string& op, int left, int right, const SourceLocation& loc);

        [[nodiscard]] int compileLiteral(const hir::Expr& expr);
        [[nodiscard]] int compileLocal(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileGlobal(const hir::Expr& expr);
        [[nodiscard]] int compileField(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileIndex(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileCall(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileMethod(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileNew(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileLambda(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileArray(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileBinaryExpr(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileUnaryExpr(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileCompareExpr(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileOptional(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileEnum(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileAssignExpr(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileCompoundAssignExpr(const mir::Body& source, const hir::Expr& expr);
        [[nodiscard]] int compileCompoundAssign(const mir::Body& source,
            mir::ExprId value, mir::ExprId target, const std::string& op, const SourceLocation& loc);

        void storeField(const mir::Body& source, const hir::Expr& target, int valueReg);
        void storeIndex(const mir::Body& source, const hir::Expr& target, int valueReg);
        void storeVariable(const std::string& name, int valueReg);
        void storeLocal(hir::LocalId local, const mir::Body& source, int valueReg);
        void bindThis(const mir::Body& source, int base, int argCount, int receiverReg, const std::vector<mir::ExprId>& args);

        void fail(const SourceLocation& loc, const std::string& message);
    };
}
