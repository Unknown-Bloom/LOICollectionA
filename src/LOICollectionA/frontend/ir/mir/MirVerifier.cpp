#include <cstddef>
#include <string>

#include "LOICollectionA/frontend/ir/mir/MirVerifier.h"

namespace LOICollection::frontend::ir::mir {
    namespace detail {
        bool verifyExpr(const Body& body, ExprId id, DiagnosticEngine& diagnostics) {
            if (!body.validExpr(id)) {
                diagnostics.addError({}, "MIR expression index out of range");
                return false;
            }

            const hir::Expr& expr = body.exprs[id];
            for (const ExprId arg : expr.args) {
                if (!body.validExpr(arg)) {
                    diagnostics.addError(expr.loc, "MIR expression operand index out of range");
                    return false;
                }
            }

            if (expr.kind == hir::ExprKind::LocalRef &&
                !body.validLocal(static_cast<LocalId>(expr.ordinal))) {
                diagnostics.addError(expr.loc, "MIR local index out of range");
                return false;
            }

            return true;
        }

        bool verifyTerminator(const Body& body, const Block& block, DiagnosticEngine& diagnostics) {
            const Terminator& terminator = block.terminator;

            switch (terminator.kind) {
                case TerminatorKind::Unreachable:
                    return true;
                case TerminatorKind::Goto:
                    if (!body.validBlock(terminator.target)) {
                        diagnostics.addError(terminator.loc, "MIR branch target out of range");
                        return false;
                    }
                    return true;
                case TerminatorKind::Branch:
                    if (!body.validBlock(terminator.target) || !body.validBlock(terminator.alternate)) {
                        diagnostics.addError(terminator.loc, "MIR branch target out of range");
                        return false;
                    }
                    return verifyExpr(body, terminator.condition, diagnostics);
                case TerminatorKind::SwitchTag:
                    if (!body.validBlock(terminator.target) || !body.validBlock(terminator.alternate)) {
                        diagnostics.addError(terminator.loc, "MIR switch target out of range");
                        return false;
                    }
                    return verifyExpr(body, terminator.value, diagnostics);
                case TerminatorKind::Return:
                case TerminatorKind::Halt:
                case TerminatorKind::Panic:
                    if (terminator.value == invalidId)
                        return true;
                    return verifyExpr(body, terminator.value, diagnostics);
            }

            return true;
        }

        bool verifyBody(const Body& body, DiagnosticEngine& diagnostics) {
            if (body.blocks.empty()) {
                diagnostics.addError({}, "MIR body has no basic blocks");
                return false;
            }

            if (body.entry != 0) {
                diagnostics.addError({}, "MIR entry block must be zero");
                return false;
            }

            for (const hir::Local& local : body.locals) {
                if (!local.type.valid()) {
                    diagnostics.addError({}, "MIR local has an invalid type");
                    return false;
                }
            }

            for (ExprId id = 0; id < body.exprs.size(); ++id) {
                if (!verifyExpr(body, id, diagnostics))
                    return false;
            }

            for (const Block& block : body.blocks) {
                for (const Statement& statement : block.statements) {
                    switch (statement.kind) {
                        case StatementKind::Evaluate:
                            if (!verifyExpr(body, statement.expr, diagnostics))
                                return false;
                            break;
                        case StatementKind::StoreLocal:
                            if (!body.validLocal(statement.local)) {
                                diagnostics.addError(statement.loc, "MIR store local out of range");
                                return false;
                            }
                            if (!verifyExpr(body, statement.expr, diagnostics))
                                return false;
                            break;
                        case StatementKind::StoreGlobal:
                            if (statement.name.empty()) {
                                diagnostics.addError(statement.loc, "MIR global store has no name");
                                return false;
                            }
                            if (!verifyExpr(body, statement.expr, diagnostics))
                                return false;
                            break;
                        case StatementKind::StoreField:
                        case StatementKind::StoreIndex:
                        case StatementKind::CompoundAssign:
                            if (!verifyExpr(body, statement.target, diagnostics) ||
                                !verifyExpr(body, statement.expr, diagnostics))
                                return false;
                            break;
                    }
                }

                if (!verifyTerminator(body, block, diagnostics))
                    return false;
            }

            return true;
        }
    }

    bool MirVerifier::verify(const Module& module, DiagnosticEngine& diagnostics) {
        if (module.bodies.empty()) {
            diagnostics.addError({}, "MIR module has no bodies");
            return false;
        }

        for (const Body& body : module.bodies) {
            if (!detail::verifyBody(body, diagnostics))
                return false;
        }

        return true;
    }
}
