#include <cstddef>
#include <string>

#include "LOICollectionA/frontend/ir/hir/HirVerifier.h"

namespace LOICollection::frontend::ir::hir {
    namespace detail {
        bool verifyExpr(const Program& program, const Body& body, ExprId id, DiagnosticEngine& diagnostics) {
            if (!body.validExpr(id)) {
                diagnostics.addError({}, "HIR expression index out of range");
                return false;
            }

            const Expr& expr = body.exprs[id];
            for (const ExprId arg : expr.args) {
                if (!body.validExpr(arg)) {
                    diagnostics.addError(expr.loc, "HIR expression operand index out of range");
                    return false;
                }
            }

            for (const LocalId local : expr.binderLocals) {
                if (!body.validLocal(local)) {
                    diagnostics.addError(expr.loc, "HIR binder local index out of range");
                    return false;
                }
            }

            if (expr.kind == ExprKind::LocalRef &&
                !body.validLocal(static_cast<LocalId>(expr.ordinal))) {
                diagnostics.addError(expr.loc, "HIR local index out of range");
                return false;
            }

            if (expr.kind == ExprKind::Lambda &&
                (expr.bodyIndex == invalidId || expr.bodyIndex >= program.bodies.size())) {
                diagnostics.addError(expr.loc, "HIR lambda body index out of range");
                return false;
            }

            if (expr.kind == ExprKind::Lambda) {
                for (const Capture& capture : expr.captures) {
                    if (!program.bodies[expr.bodyIndex].validLocal(capture.target)) {
                        diagnostics.addError(expr.loc, "HIR lambda capture index out of range");
                        return false;
                    }
                }
            }

            return true;
        }

        bool verifyStmt(const Program& program, const Body& body, StmtId id, DiagnosticEngine& diagnostics) {
            if (!body.validStmt(id)) {
                diagnostics.addError({}, "HIR statement index out of range");
                return false;
            }

            const Stmt& statement = body.stmts[id];

            if (statement.value != invalidId && !body.validExpr(statement.value)) {
                diagnostics.addError(statement.loc, "HIR statement value index out of range");
                return false;
            }

            if (statement.target != invalidId && !body.validExpr(statement.target)) {
                diagnostics.addError(statement.loc, "HIR statement target index out of range");
                return false;
            }

            if (statement.condition != invalidId && !body.validExpr(statement.condition)) {
                diagnostics.addError(statement.loc, "HIR statement condition index out of range");
                return false;
            }

            for (const StmtId child : statement.body) {
                if (!verifyStmt(program, body, child, diagnostics))
                    return false;
            }

            for (const StmtId child : statement.alternate) {
                if (!verifyStmt(program, body, child, diagnostics))
                    return false;
            }

            for (const MatchArm& arm : statement.arms) {
                if (arm.guard != invalidId && !body.validExpr(arm.guard)) {
                    diagnostics.addError(arm.loc, "HIR match guard index out of range");
                    return false;
                }

                for (const LocalId local : arm.binderLocals) {
                    if (!body.validLocal(local)) {
                        diagnostics.addError(arm.loc, "HIR match binder index out of range");
                        return false;
                    }
                }

                for (const StmtId child : arm.body) {
                    if (!verifyStmt(program, body, child, diagnostics))
                        return false;
                }
            }

            if (statement.kind == StmtKind::ForIn) {
                if (!body.validLocal(statement.elementLocal)) {
                    diagnostics.addError(statement.loc, "HIR for-in element index out of range");
                    return false;
                }

                if (statement.indexLocal != invalidId && !body.validLocal(statement.indexLocal)) {
                    diagnostics.addError(statement.loc, "HIR for-in index index out of range");
                    return false;
                }
            }

            return true;
        }

        bool verifyBody(const Program& program, const Body& body, DiagnosticEngine& diagnostics) {
            for (const Local& local : body.locals) {
                if (!local.type.valid()) {
                    diagnostics.addError({}, "HIR local has an invalid type");
                    return false;
                }
            }

            for (ExprId id = 0; id < body.exprs.size(); ++id) {
                if (!verifyExpr(program, body, id, diagnostics))
                    return false;
            }

            for (StmtId id = 0; id < body.stmts.size(); ++id) {
                if (!verifyStmt(program, body, id, diagnostics))
                    return false;
            }

            for (const StmtId root : body.root) {
                if (!body.validStmt(root)) {
                    diagnostics.addError({}, "HIR root statement index out of range");
                    return false;
                }
            }

            return true;
        }
    }

    bool HirVerifier::verify(const Program& program, DiagnosticEngine& diagnostics) {
        if (!detail::verifyBody(program, program.topLevel, diagnostics))
            return false;

        for (const Body& body : program.bodies) {
            if (!detail::verifyBody(program, body, diagnostics))
                return false;
        }

        for (const Function& function : program.functions) {
            if (function.bodyIndex == invalidId || function.bodyIndex >= program.bodies.size()) {
                diagnostics.addError({}, "HIR function body index out of range");
                return false;
            }
        }

        for (const Class& cls : program.classes) {
            for (const Method& method : cls.methods) {
                if (method.bodyIndex == invalidId || method.bodyIndex >= program.bodies.size()) {
                    diagnostics.addError({}, "HIR method body index out of range");
                    return false;
                }
            }

            for (const int ordinal : cls.methodTable) {
                if (ordinal < 0 || static_cast<std::size_t>(ordinal) >= program.bodies.size()) {
                    diagnostics.addError({}, "HIR method table index out of range");
                    return false;
                }
            }

            for (const int ordinal : cls.staticMethodTable) {
                if (ordinal < 0 || static_cast<std::size_t>(ordinal) >= program.bodies.size()) {
                    diagnostics.addError({}, "HIR static method table index out of range");
                    return false;
                }
            }

            if (cls.constructorOrdinal < -1 ||
                (cls.constructorOrdinal >= 0 &&
                 static_cast<std::size_t>(cls.constructorOrdinal) >= program.bodies.size())) {
                diagnostics.addError({}, "HIR constructor index out of range");
                return false;
            }
        }

        return true;
    }
}
