#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Iteration.h"

#include "LOICollectionA/frontend/ir/mir/MirBuilder.h"

namespace LOICollection::frontend::ir::mir {
    MirBuilder::MirBuilder(const hir::Program& source, DiagnosticEngine& diag)
        : program(source), diagnostics(diag) {}

    Module MirBuilder::build() {
        Body topLevel;
        topLevel.locals = this->program.topLevel.locals;
        topLevel.paramCount = this->program.topLevel.paramCount;
        topLevel.isTopLevel = true;
        topLevel.returnsValue = this->program.topLevel.returnsValue;
        this->module.bodies.push_back(std::move(topLevel));

        for (const hir::Body& source : this->program.bodies) {
            Body target;
            target.locals = source.locals;
            target.paramCount = source.paramCount;
            target.isTopLevel = source.isTopLevel;
            target.returnsValue = source.returnsValue;
            this->module.bodies.push_back(std::move(target));
        }

        this->buildBody(this->module.bodies[0], this->program.topLevel);
        for (std::size_t index = 0; index < this->program.bodies.size(); ++index)
            this->buildBody(this->module.bodies[index + 1], this->program.bodies[index]);

        return std::move(this->module);
    }

    BlockId MirBuilder::newBlock() {
        this->body->blocks.emplace_back();
        return static_cast<BlockId>(this->body->blocks.size() - 1);
    }

    void MirBuilder::selectBlock(BlockId block) {
        this->currentBlock = block;
        this->finished = false;
    }

    void MirBuilder::terminate(Terminator terminator) {
        if (this->finished || !this->body->validBlock(this->currentBlock))
            return;

        this->body->blocks[this->currentBlock].terminator = std::move(terminator);
        this->finished = true;
    }

    void MirBuilder::emit(Statement statement) {
        if (this->finished || !this->body->validBlock(this->currentBlock))
            return;

        this->body->blocks[this->currentBlock].statements.push_back(std::move(statement));
    }

    ExprId MirBuilder::addExpr(hir::Expr value) {
        this->body->exprs.push_back(std::move(value));
        return static_cast<ExprId>(this->body->exprs.size() - 1);
    }

    LocalId MirBuilder::addTemp(ty::Ty type) {
        hir::Local local;
        local.type = type.valid() ? type : this->program.types->dynamic();
        this->body->locals.push_back(std::move(local));
        return static_cast<LocalId>(this->body->locals.size() - 1);
    }

    ExprId MirBuilder::literal(const ValueNode::ValueType& value, ty::Ty type, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::Literal;
        expr.type = type;
        expr.loc = loc;
        expr.literal = value;
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::localRef(LocalId local, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::LocalRef;
        expr.loc = loc;
        expr.ordinal = static_cast<int>(local);
        expr.type = this->body->validLocal(local) ? this->body->locals[local].type : this->program.types->dynamic();
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::preservedLocal(LocalId local, const SourceLocation& loc) {
        const ExprId id = this->localRef(local, loc);
        this->body->exprs[id].flag2 = true;
        return id;
    }

    ExprId MirBuilder::globalRef(const std::string& name, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::GlobalRef;
        expr.loc = loc;
        expr.name = name;
        expr.type = this->program.types->dynamic();
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::defaultExpr(const SourceLocation& loc) {
        return this->literal(std::string(""), this->program.types->primitive(ty::TyKind::String), loc);
    }

    ExprId MirBuilder::noneExpr(const SourceLocation& loc) {
        return this->literal(std::monostate{}, this->program.types->primitive(ty::TyKind::None), loc);
    }

    const hir::Expr& MirBuilder::expr(ExprId id) const {
        static const hir::Expr invalid{};
        if (!this->body || !this->body->validExpr(id))
            return invalid;

        return this->body->exprs[id];
    }

    ExprId MirBuilder::makeIsNone(ExprId value, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::IsNone;
        expr.type = this->program.types->primitive(ty::TyKind::Bool);
        expr.loc = loc;
        expr.args.push_back(value);
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::makeUnwrap(ExprId value, ty::Ty type, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::Unwrap;
        expr.type = type;
        expr.loc = loc;
        expr.args.push_back(value);
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::makeLen(ExprId value, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::Len;
        expr.type = this->program.types->primitive(ty::TyKind::Int);
        expr.loc = loc;
        expr.args.push_back(value);
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::makeIndex(ExprId target, ExprId index, ty::Ty type, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::Index;
        expr.type = type;
        expr.loc = loc;
        expr.args.push_back(target);
        expr.args.push_back(index);
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::makeField(ExprId target, const std::string& name, int slot, ty::Ty type, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::Field;
        expr.type = type;
        expr.loc = loc;
        expr.name = name;
        expr.slot = slot;
        expr.args.push_back(target);
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::makeBinary(const std::string& op, ExprId left, ExprId right, ty::Ty type, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::Binary;
        expr.type = type;
        expr.loc = loc;
        expr.op = op;
        expr.args.push_back(left);
        expr.args.push_back(right);
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::makeCompare(const std::string& op, ExprId left, ExprId right, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::Compare;
        expr.type = this->program.types->primitive(ty::TyKind::Bool);
        expr.loc = loc;
        expr.op = op;
        expr.args.push_back(left);
        expr.args.push_back(right);
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::makeInstanceOf(ExprId value, const std::string& className, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::InstanceOf;
        expr.type = this->program.types->primitive(ty::TyKind::Bool);
        expr.loc = loc;
        expr.className = className;
        expr.args.push_back(value);
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::makePayload(ExprId value, int slot, ty::Ty type, const SourceLocation& loc) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::EnumPayload;
        expr.type = type;
        expr.loc = loc;
        expr.slot = slot;
        expr.args.push_back(value);
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::makeNativeCall(
        const std::string& className, const std::string& name, std::vector<ExprId> args,
        bool isStatic, ty::Ty type, const SourceLocation& loc
    ) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::NativeCall;
        expr.type = type;
        expr.loc = loc;
        expr.className = className;
        expr.name = name;
        expr.flag = isStatic;
        expr.args = std::move(args);
        return this->addExpr(std::move(expr));
    }

    ExprId MirBuilder::makeByNameCall(
        const std::string& name, std::vector<ExprId> args, ty::Ty type, const SourceLocation& loc
    ) {
        hir::Expr expr;
        expr.kind = hir::ExprKind::MethodByName;
        expr.type = type;
        expr.loc = loc;
        expr.name = name;
        expr.args = std::move(args);
        return this->addExpr(std::move(expr));
    }

    bool MirBuilder::isControl(hir::ExprKind kind, bool safe) const {
        switch (kind) {
            case hir::ExprKind::If:
            case hir::ExprKind::Match:
            case hir::ExprKind::Coalesce:
            case hir::ExprKind::Logical:
            case hir::ExprKind::Try:
            case hir::ExprKind::SafeField:
            case hir::ExprKind::SafeIndex:
            case hir::ExprKind::LetPattern:
                return true;
            case hir::ExprKind::Field:
            case hir::ExprKind::Index:
            case hir::ExprKind::TypeOf:
            case hir::ExprKind::HasValue:
            case hir::ExprKind::Unwrap:
                return safe;
            default:
                return false;
        }
    }

    ExprId MirBuilder::lowerExpr(hir::ExprId id) {
        hir::Expr value = this->hirBody->exprs[id];

        for (ExprId& arg : value.args)
            arg = this->lowerValue(arg);

        return this->addExpr(std::move(value));
    }

    ExprId MirBuilder::lowerValue(hir::ExprId id) {
        const hir::Expr& value = this->hirBody->exprs[id];

        if (value.kind == hir::ExprKind::New && !value.body.empty())
            return this->lowerNew(id);

        if (this->isControl(value.kind, value.flag)) {
            switch (value.kind) {
                case hir::ExprKind::If:
                    return this->lowerIf(id);
                case hir::ExprKind::Match:
                    return this->lowerMatch(id);
                case hir::ExprKind::Coalesce:
                    return this->lowerCoalesce(id);
                case hir::ExprKind::Logical:
                    return this->lowerLogical(id);
                case hir::ExprKind::Try:
                    return this->lowerTry(id);
                default:
                    return this->lowerSafe(id);
            }
        }

        return this->lowerExpr(id);
    }

    ExprId MirBuilder::lowerNew(hir::ExprId id) {
        const hir::Expr& value = this->hirBody->exprs[id];

        hir::Expr constructed = value;
        constructed.body.clear();
        constructed.receiver = hir::invalidId;

        for (ExprId& arg : constructed.args)
            arg = this->lowerValue(arg);

        const ExprId object = this->addExpr(std::move(constructed));
        const LocalId local = this->addTemp(value.type);

        Statement store;
        store.kind = StatementKind::StoreLocal;
        store.loc = value.loc;
        store.local = local;
        store.expr = object;
        this->emit(std::move(store));

        if (value.receiver != hir::invalidId) {
            Statement bind;
            bind.kind = StatementKind::StoreLocal;
            bind.loc = value.loc;
            bind.local = value.receiver;
            bind.expr = this->localRef(local, value.loc);
            this->emit(std::move(bind));
        }

        this->lowerBlock(value.body);
        return this->localRef(local, value.loc);
    }

    ExprId MirBuilder::lowerIf(hir::ExprId id) {
        const hir::Expr& value = this->hirBody->exprs[id];

        const LocalId result = this->addTemp(value.type);
        const BlockId thenBlock = this->newBlock();
        const BlockId elseBlock = this->newBlock();
        const BlockId joinBlock = this->newBlock();

        this->lowerCondition(value.args[0], thenBlock, elseBlock);

        this->selectBlock(thenBlock);
        const ExprId thenValue = this->lowerBlockValue(value.body, true);
        if (!this->finished) {
            Statement store;
            store.kind = StatementKind::StoreLocal;
            store.loc = value.loc;
            store.local = result;
            store.expr = thenValue;
            this->emit(std::move(store));
            this->terminate({ TerminatorKind::Goto, value.loc, joinBlock });
        }

        this->selectBlock(elseBlock);
        const ExprId elseValue = this->lowerBlockValue(value.alternate, true);
        if (!this->finished) {
            Statement store;
            store.kind = StatementKind::StoreLocal;
            store.loc = value.loc;
            store.local = result;
            store.expr = elseValue;
            this->emit(std::move(store));
            this->terminate({ TerminatorKind::Goto, value.loc, joinBlock });
        }

        this->selectBlock(joinBlock);
        return this->localRef(result, value.loc);
    }

    ExprId MirBuilder::lowerMatch(hir::ExprId id) {
        const hir::Expr& value = this->hirBody->exprs[id];

        const ExprId targetValue = this->lowerValue(value.args[0]);
        const LocalId target = this->addTemp(this->expr(targetValue).type);

        Statement targetStore;
        targetStore.kind = StatementKind::StoreLocal;
        targetStore.loc = value.loc;
        targetStore.local = target;
        targetStore.expr = targetValue;
        this->emit(std::move(targetStore));

        const LocalId result = this->addTemp(value.type);

        const bool covered = std::any_of(value.arms.begin(), value.arms.end(), [](const hir::MatchArm& arm) -> bool {
            return (arm.pattern == hir::MatchArm::Pattern::Bind ||
                    arm.pattern == hir::MatchArm::Pattern::Wildcard) &&
                   arm.guard == hir::invalidId;
        });

        if (!covered) {
            Statement initial;
            initial.kind = StatementKind::StoreLocal;
            initial.loc = value.loc;
            initial.local = result;
            initial.expr = this->noneExpr(value.loc);
            this->emit(std::move(initial));
        }

        const BlockId joinBlock = this->newBlock();

        for (const hir::MatchArm& arm : value.arms) {
            const BlockId armBlock = this->newBlock();
            const BlockId nextBlock = this->newBlock();

            switch (arm.pattern) {
                case hir::MatchArm::Pattern::Literal: {
                    ExprId condition = hir::invalidId;
                    if (std::holds_alternative<std::monostate>(arm.literal)) {
                        condition = this->makeIsNone(this->preservedLocal(target, arm.loc), arm.loc);
                    } else {
                        const ExprId literal = this->literal(arm.literal, this->program.types->dynamic(), arm.loc);
                        condition = this->makeCompare("==", this->preservedLocal(target, arm.loc), literal, arm.loc);
                    }

                    this->terminate({ TerminatorKind::Branch, arm.loc, armBlock, nextBlock, condition });
                    break;
                }
                case hir::MatchArm::Pattern::Type: {
                    const ExprId condition = this->makeInstanceOf(this->preservedLocal(target, arm.loc), arm.className, arm.loc);
                    this->terminate({ TerminatorKind::Branch, arm.loc, armBlock, nextBlock, condition });
                    break;
                }
                case hir::MatchArm::Pattern::EnumVariant: {
                    auto it = this->program.enumVariants.find(arm.variantName);
                    if (it == this->program.enumVariants.end()) {
                        this->diagnostics.addError(arm.loc, "Unknown enum variant: " + arm.variantName);
                        this->terminate({ TerminatorKind::Goto, arm.loc, nextBlock });
                        break;
                    }

                    Terminator terminator;
                    terminator.kind = TerminatorKind::SwitchTag;
                    terminator.loc = arm.loc;
                    terminator.target = armBlock;
                    terminator.alternate = nextBlock;
                    terminator.value = this->preservedLocal(target, arm.loc);
                    terminator.tagEnum = it->second.first;
                    terminator.tag = it->second.second;
                    this->terminate(std::move(terminator));
                    break;
                }
                case hir::MatchArm::Pattern::Bind:
                case hir::MatchArm::Pattern::Wildcard:
                    this->terminate({ TerminatorKind::Goto, arm.loc, armBlock });
                    break;
            }

            this->selectBlock(armBlock);

            if (!arm.binder.empty() && !arm.binderLocals.empty()) {
                Statement bind;
                bind.kind = StatementKind::StoreLocal;
                bind.loc = arm.loc;
                bind.local = arm.binderLocals[0];
                bind.expr = this->preservedLocal(target, arm.loc);
                this->emit(std::move(bind));
            }

            const std::size_t payloadStart = arm.binder.empty() ? 0 : 1;
            for (std::size_t index = payloadStart; index < arm.binderLocals.size(); ++index) {
                const LocalId binder = arm.binderLocals[index];
                Statement bind;
                bind.kind = StatementKind::StoreLocal;
                bind.loc = arm.loc;
                bind.local = binder;
                bind.expr = this->makePayload(
                    this->preservedLocal(target, arm.loc),
                    static_cast<int>(index - payloadStart),
                    this->body->locals[binder].type,
                    arm.loc
                );
                this->emit(std::move(bind));
            }

            if (arm.guard != hir::invalidId) {
                const ExprId guard = this->lowerValue(arm.guard);
                const BlockId guardBlock = this->newBlock();
                this->terminate({ TerminatorKind::Branch, arm.loc, guardBlock, nextBlock, guard });
                this->selectBlock(guardBlock);
            }

            const ExprId bodyValue = this->lowerBlockValue(arm.body, true);
            if (!this->finished) {
                Statement store;
                store.kind = StatementKind::StoreLocal;
                store.loc = arm.loc;
                store.local = result;
                store.expr = bodyValue;
                this->emit(std::move(store));
                this->terminate({ TerminatorKind::Goto, arm.loc, joinBlock });
            }

            this->selectBlock(nextBlock);
        }

        this->terminate({ TerminatorKind::Goto, value.loc, joinBlock });
        this->selectBlock(joinBlock);
        return this->localRef(result, value.loc);
    }

    ExprId MirBuilder::lowerCoalesce(hir::ExprId id) {
        const hir::Expr& value = this->hirBody->exprs[id];

        const ExprId leftValue = this->lowerValue(value.args[0]);
        const LocalId left = this->addTemp(this->expr(leftValue).type);

        Statement storeLeft;
        storeLeft.kind = StatementKind::StoreLocal;
        storeLeft.loc = value.loc;
        storeLeft.local = left;
        storeLeft.expr = leftValue;
        this->emit(std::move(storeLeft));

        const LocalId result = this->addTemp(value.type);
        const BlockId rightBlock = this->newBlock();
        const BlockId keepBlock = this->newBlock();
        const BlockId joinBlock = this->newBlock();

        const ExprId condition = this->makeIsNone(this->localRef(left, value.loc), value.loc);
        this->terminate({ TerminatorKind::Branch, value.loc, rightBlock, keepBlock, condition });

        this->selectBlock(keepBlock);
        Statement keep;
        keep.kind = StatementKind::StoreLocal;
        keep.loc = value.loc;
        keep.local = result;
        keep.expr = this->localRef(left, value.loc);
        this->emit(std::move(keep));
        this->terminate({ TerminatorKind::Goto, value.loc, joinBlock });

        this->selectBlock(rightBlock);
        Statement right;
        right.kind = StatementKind::StoreLocal;
        right.loc = value.loc;
        right.local = result;
        right.expr = this->lowerValue(value.args[1]);
        this->emit(std::move(right));
        this->terminate({ TerminatorKind::Goto, value.loc, joinBlock });

        this->selectBlock(joinBlock);
        return this->localRef(result, value.loc);
    }

    ExprId MirBuilder::lowerLogical(hir::ExprId id) {
        const hir::Expr& value = this->hirBody->exprs[id];
        const bool isAnd = value.op == "&&";

        const ExprId leftValue = this->lowerValue(value.args[0]);
        const LocalId left = this->addTemp(this->expr(leftValue).type);

        Statement storeLeft;
        storeLeft.kind = StatementKind::StoreLocal;
        storeLeft.loc = value.loc;
        storeLeft.local = left;
        storeLeft.expr = leftValue;
        this->emit(std::move(storeLeft));

        const LocalId result = this->addTemp(this->program.types->primitive(ty::TyKind::Bool));
        const BlockId rightBlock = this->newBlock();
        const BlockId shortBlock = this->newBlock();
        const BlockId joinBlock = this->newBlock();

        Terminator branch;
        branch.kind = TerminatorKind::Branch;
        branch.loc = value.loc;
        branch.condition = this->localRef(left, value.loc);
        branch.target = isAnd ? rightBlock : shortBlock;
        branch.alternate = isAnd ? shortBlock : rightBlock;
        this->terminate(std::move(branch));

        this->selectBlock(shortBlock);
        Statement shortStore;
        shortStore.kind = StatementKind::StoreLocal;
        shortStore.loc = value.loc;
        shortStore.local = result;
        shortStore.expr = this->literal(!isAnd, this->program.types->primitive(ty::TyKind::Bool), value.loc);
        this->emit(std::move(shortStore));
        this->terminate({ TerminatorKind::Goto, value.loc, joinBlock });

        this->selectBlock(rightBlock);
        const ExprId rightValue = this->lowerValue(value.args[1]);
        const ExprId logical = this->makeBinary(value.op, this->localRef(left, value.loc), rightValue,
            this->program.types->primitive(ty::TyKind::Bool), value.loc);

        Statement rightStore;
        rightStore.kind = StatementKind::StoreLocal;
        rightStore.loc = value.loc;
        rightStore.local = result;
        rightStore.expr = logical;
        this->emit(std::move(rightStore));
        this->terminate({ TerminatorKind::Goto, value.loc, joinBlock });

        this->selectBlock(joinBlock);
        return this->localRef(result, value.loc);
    }

    ExprId MirBuilder::lowerTry(hir::ExprId id) {
        const hir::Expr& value = this->hirBody->exprs[id];

        const ExprId operand = this->lowerValue(value.args[0]);
        const LocalId input = this->addTemp(this->expr(operand).type);

        Statement store;
        store.kind = StatementKind::StoreLocal;
        store.loc = value.loc;
        store.local = input;
        store.expr = operand;
        this->emit(std::move(store));

        const LocalId result = this->addTemp(value.type);
        const BlockId noneBlock = this->newBlock();
        const BlockId valueBlock = this->newBlock();
        const BlockId joinBlock = this->newBlock();

        const ExprId condition = this->makeIsNone(this->localRef(input, value.loc), value.loc);
        this->terminate({ TerminatorKind::Branch, value.loc, noneBlock, valueBlock, condition });

        this->selectBlock(noneBlock);
        this->terminate({ TerminatorKind::Return, value.loc, invalidId, invalidId, invalidId, this->noneExpr(value.loc) });

        this->selectBlock(valueBlock);
        Statement unwrap;
        unwrap.kind = StatementKind::StoreLocal;
        unwrap.loc = value.loc;
        unwrap.local = result;
        unwrap.expr = this->makeUnwrap(this->localRef(input, value.loc), value.type, value.loc);
        this->emit(std::move(unwrap));
        this->terminate({ TerminatorKind::Goto, value.loc, joinBlock });

        this->selectBlock(joinBlock);
        return this->localRef(result, value.loc);
    }

    ExprId MirBuilder::lowerSafe(hir::ExprId id) {
        const hir::Expr& value = this->hirBody->exprs[id];

        const ExprId targetValue = this->lowerValue(value.args[0]);
        const LocalId target = this->addTemp(this->expr(targetValue).type);

        Statement targetStore;
        targetStore.kind = StatementKind::StoreLocal;
        targetStore.loc = value.loc;
        targetStore.local = target;
        targetStore.expr = targetValue;
        this->emit(std::move(targetStore));

        const LocalId result = this->addTemp(value.type);
        const BlockId noneBlock = this->newBlock();
        const BlockId valueBlock = this->newBlock();
        const BlockId joinBlock = this->newBlock();

        const ExprId condition = this->makeIsNone(this->localRef(target, value.loc), value.loc);
        this->terminate({ TerminatorKind::Branch, value.loc, noneBlock, valueBlock, condition });

        this->selectBlock(noneBlock);
        Statement none;
        none.kind = StatementKind::StoreLocal;
        none.loc = value.loc;
        none.local = result;
        none.expr = this->noneExpr(value.loc);
        this->emit(std::move(none));
        this->terminate({ TerminatorKind::Goto, value.loc, joinBlock });

        this->selectBlock(valueBlock);
        hir::Expr computed = value;
        computed.flag = false;
        computed.flag2 = false;
        if (computed.kind == hir::ExprKind::SafeField)
            computed.kind = hir::ExprKind::Field;
        else if (computed.kind == hir::ExprKind::SafeIndex)
            computed.kind = hir::ExprKind::Index;

        computed.args[0] = this->localRef(target, value.loc);
        for (std::size_t index = 1; index < computed.args.size(); ++index)
            computed.args[index] = this->lowerValue(value.args[index]);

        Statement store;
        store.kind = StatementKind::StoreLocal;
        store.loc = value.loc;
        store.local = result;
        store.expr = this->addExpr(std::move(computed));
        this->emit(std::move(store));
        this->terminate({ TerminatorKind::Goto, value.loc, joinBlock });

        this->selectBlock(joinBlock);
        return this->localRef(result, value.loc);
    }

    void MirBuilder::lowerCondition(hir::ExprId condition, BlockId trueBlock, BlockId falseBlock) {
        if (this->hirBody->exprs[condition].kind == hir::ExprKind::LetPattern) {
            this->lowerLetPattern(condition, trueBlock, falseBlock);
            return;
        }

        Terminator terminator;
        terminator.kind = TerminatorKind::Branch;
        terminator.loc = this->hirBody->exprs[condition].loc;
        terminator.target = trueBlock;
        terminator.alternate = falseBlock;
        terminator.condition = this->lowerValue(condition);
        this->terminate(std::move(terminator));
    }

    void MirBuilder::lowerLetPattern(hir::ExprId condition, BlockId trueBlock, BlockId falseBlock) {
        const hir::Expr& pattern = this->hirBody->exprs[condition];
        const ExprId inputValue = this->lowerValue(pattern.args[0]);
        const LocalId input = this->addTemp(this->expr(inputValue).type);

        Statement store;
        store.kind = StatementKind::StoreLocal;
        store.loc = pattern.loc;
        store.local = input;
        store.expr = inputValue;
        this->emit(std::move(store));

        const BlockId bindBlock = this->newBlock();

        if (pattern.flag) {
            auto it = this->program.enumVariants.find(pattern.name);
            if (it == this->program.enumVariants.end()) {
                this->diagnostics.addError(pattern.loc, "Unknown enum variant: " + pattern.name);
                this->terminate({ TerminatorKind::Goto, pattern.loc, falseBlock });
                return;
            }

            Terminator terminator;
            terminator.kind = TerminatorKind::SwitchTag;
            terminator.loc = pattern.loc;
            terminator.target = bindBlock;
            terminator.alternate = falseBlock;
            terminator.value = this->localRef(input, pattern.loc);
            terminator.tagEnum = it->second.first;
            terminator.tag = it->second.second;
            this->terminate(std::move(terminator));
        } else {
            this->terminate({ TerminatorKind::Goto, pattern.loc, bindBlock });
        }

        this->selectBlock(bindBlock);

        for (std::size_t index = 0; index < pattern.binderLocals.size(); ++index) {
            const LocalId binder = pattern.binderLocals[index];

            Statement bind;
            bind.kind = StatementKind::StoreLocal;
            bind.loc = pattern.loc;
            bind.local = binder;

            if (pattern.flag) {
                bind.expr = this->makePayload(
                    this->localRef(input, pattern.loc), static_cast<int>(index),
                    this->body->locals[binder].type, pattern.loc
                );
            } else {
                bind.expr = this->localRef(input, pattern.loc);
            }

            this->emit(std::move(bind));
        }

        this->terminate({ TerminatorKind::Goto, pattern.loc, trueBlock });
    }

    ExprId MirBuilder::lowerStore(const hir::Stmt& statement) {
        const ExprId value = statement.value != hir::invalidId
            ? this->lowerValue(statement.value)
            : this->defaultExpr(statement.loc);
        const ExprId targetId = this->lowerValue(statement.target);
        const hir::Expr target = this->expr(targetId);

        switch (target.kind) {
            case hir::ExprKind::LocalRef: {
                Statement store;
                store.kind = StatementKind::StoreLocal;
                store.loc = statement.loc;
                store.local = static_cast<LocalId>(target.ordinal);
                store.expr = value;
                this->emit(std::move(store));
                break;
            }
            case hir::ExprKind::GlobalRef: {
                Statement store;
                store.kind = StatementKind::StoreGlobal;
                store.loc = statement.loc;
                store.name = target.name;
                store.expr = value;
                this->emit(std::move(store));
                break;
            }
            case hir::ExprKind::Field:
            case hir::ExprKind::SafeField: {
                const ExprId object = target.args[0];
                Statement store;
                store.kind = StatementKind::StoreField;
                store.loc = statement.loc;
                store.target = this->makeField(object, target.name, target.slot, target.type, target.loc);
                store.expr = value;
                this->emit(std::move(store));
                break;
            }
            case hir::ExprKind::Index:
            case hir::ExprKind::SafeIndex: {
                const ExprId object = target.args[0];
                const ExprId index = target.args[1];
                Statement store;
                store.kind = StatementKind::StoreIndex;
                store.loc = statement.loc;
                store.target = this->makeIndex(object, index, target.type, target.loc);
                store.expr = value;
                this->emit(std::move(store));
                break;
            }
            default:
                this->diagnostics.addError(statement.loc, "Invalid assignment target");
                break;
        }

        return value;
    }

    ExprId MirBuilder::lowerBlockValue(const std::vector<hir::StmtId>& statements, bool keepDeclaration) {
        if (statements.empty())
            return this->defaultExpr({});

        for (std::size_t index = 0; index < statements.size(); ++index) {
            const hir::Stmt& statement = this->hirBody->stmts[statements[index]];
            const bool last = index + 1 == statements.size();

            if (!last) {
                this->lowerStmt(statement);
                continue;
            }

            switch (statement.kind) {
                case hir::StmtKind::Expr:
                    return this->lowerValue(statement.value);
                case hir::StmtKind::Block:
                    return this->lowerBlockValue(statement.body, keepDeclaration);
                case hir::StmtKind::Let:
                case hir::StmtKind::Assign: {
                    const ExprId value = this->lowerStore(statement);
                    return keepDeclaration ? value : this->defaultExpr(statement.loc);
                }
                default:
                    this->lowerStmt(statement);
                    return this->defaultExpr(statement.loc);
            }
        }

        return this->defaultExpr({});
    }

    void MirBuilder::lowerBlock(const std::vector<hir::StmtId>& statements) {
        for (const hir::StmtId id : statements)
            this->lowerStmt(this->hirBody->stmts[id]);
    }

    void MirBuilder::lowerStmt(const hir::Stmt& statement) {
        switch (statement.kind) {
            case hir::StmtKind::Let:
            case hir::StmtKind::Assign:
                static_cast<void>(this->lowerStore(statement));
                break;
            case hir::StmtKind::CompoundAssign: {
                Statement store;
                store.kind = StatementKind::CompoundAssign;
                store.loc = statement.loc;
                store.target = this->lowerValue(statement.target);
                store.expr = this->lowerValue(statement.value);
                store.op = statement.op;
                this->emit(std::move(store));
                break;
            }
            case hir::StmtKind::Expr: {
                const ExprId value = this->lowerValue(statement.value);
                if (value == invalidId)
                    break;

                Statement evaluate;
                evaluate.kind = StatementKind::Evaluate;
                evaluate.loc = statement.loc;
                evaluate.expr = value;
                this->emit(std::move(evaluate));
                break;
            }
            case hir::StmtKind::Block:
                this->lowerBlock(statement.body);
                break;
            case hir::StmtKind::While:
                this->lowerWhile(statement);
                break;
            case hir::StmtKind::For:
                this->lowerFor(statement);
                break;
            case hir::StmtKind::ForIn:
                this->lowerForIn(statement);
                break;
            case hir::StmtKind::Match:
            case hir::StmtKind::If: {
                const ExprId value = this->lowerValue(statement.value);
                if (value == invalidId)
                    break;

                Statement evaluate;
                evaluate.kind = StatementKind::Evaluate;
                evaluate.loc = statement.loc;
                evaluate.expr = value;
                this->emit(std::move(evaluate));
                break;
            }
            case hir::StmtKind::Return: {
                const ExprId value = statement.value != hir::invalidId
                    ? this->lowerValue(statement.value)
                    : this->defaultExpr(statement.loc);
                this->terminate({ TerminatorKind::Return, statement.loc, invalidId, invalidId, invalidId, value });
                break;
            }
            case hir::StmtKind::Break:
            case hir::StmtKind::Continue: {
                if (this->loops.empty()) {
                    this->diagnostics.addError(statement.loc, "'break' or 'continue' outside a loop");
                    break;
                }

                const LoopContext& loop = this->loops.back();
                this->terminate({
                    TerminatorKind::Goto,
                    statement.loc,
                    statement.kind == hir::StmtKind::Break ? loop.breakTarget : loop.continueTarget
                });
                break;
            }
            case hir::StmtKind::Panic: {
                const ExprId value = statement.value != hir::invalidId
                    ? this->lowerValue(statement.value)
                    : this->defaultExpr(statement.loc);
                this->terminate({ TerminatorKind::Panic, statement.loc, invalidId, invalidId, invalidId, value });
                break;
            }
        }
    }

    void MirBuilder::lowerWhile(const hir::Stmt& statement) {
        const BlockId conditionBlock = this->newBlock();
        const BlockId bodyBlock = this->newBlock();
        const BlockId exitBlock = this->newBlock();

        this->terminate({ TerminatorKind::Goto, statement.loc, conditionBlock });
        this->selectBlock(conditionBlock);
        this->lowerCondition(statement.condition, bodyBlock, exitBlock);

        this->selectBlock(bodyBlock);
        this->loops.push_back({ exitBlock, conditionBlock });
        this->lowerBlock(statement.body);
        this->loops.pop_back();
        if (!this->finished)
            this->terminate({ TerminatorKind::Goto, statement.loc, conditionBlock });

        this->selectBlock(exitBlock);
    }

    void MirBuilder::lowerFor(const hir::Stmt& statement) {
        if (statement.target != hir::invalidId) {
            const ExprId init = this->lowerValue(statement.target);
            if (!this->finished) {
                Statement evaluate;
                evaluate.kind = StatementKind::Evaluate;
                evaluate.loc = statement.loc;
                evaluate.expr = init;
                this->emit(std::move(evaluate));
            }
        }

        const BlockId conditionBlock = this->newBlock();
        const BlockId bodyBlock = this->newBlock();
        const BlockId stepBlock = this->newBlock();
        const BlockId exitBlock = this->newBlock();

        this->terminate({ TerminatorKind::Goto, statement.loc, conditionBlock });
        this->selectBlock(conditionBlock);

        if (statement.condition != hir::invalidId)
            this->lowerCondition(statement.condition, bodyBlock, exitBlock);
        else
            this->terminate({ TerminatorKind::Goto, statement.loc, bodyBlock });

        this->selectBlock(bodyBlock);
        this->loops.push_back({ exitBlock, stepBlock });
        this->lowerBlock(statement.body);
        this->loops.pop_back();
        if (!this->finished)
            this->terminate({ TerminatorKind::Goto, statement.loc, stepBlock });

        this->selectBlock(stepBlock);
        if (statement.value != hir::invalidId) {
            const ExprId step = this->lowerValue(statement.value);
            if (!this->finished) {
                Statement evaluate;
                evaluate.kind = StatementKind::Evaluate;
                evaluate.loc = statement.loc;
                evaluate.expr = step;
                this->emit(std::move(evaluate));
            }
        }
        if (!this->finished)
            this->terminate({ TerminatorKind::Goto, statement.loc, conditionBlock });

        this->selectBlock(exitBlock);
    }

    void MirBuilder::lowerForIn(const hir::Stmt& statement) {
        const auto shape = static_cast<IterableShape>(statement.shape);
        const LocalId element = statement.elementLocal;
        const ty::Ty elementType = this->body->locals[element].type;

        BlockId loopBlock = invalidId;
        BlockId bodyBlock = invalidId;
        BlockId exitBlock = invalidId;

        auto storeState = [this, &statement](LocalId local, ExprId value) -> void {
            Statement store;
            store.kind = StatementKind::StoreLocal;
            store.loc = statement.loc;
            store.local = local;
            store.expr = value;
            this->emit(std::move(store));
        };

        auto storeElement = [this, &statement, element](ExprId value, LocalId indexSource) -> void {
            Statement store;
            store.kind = StatementKind::StoreLocal;
            store.loc = statement.loc;
            store.local = element;
            store.expr = value;
            this->emit(std::move(store));

            if (statement.indexLocal != hir::invalidId) {
                Statement index;
                index.kind = StatementKind::StoreLocal;
                index.loc = statement.loc;
                index.local = statement.indexLocal;
                index.expr = this->localRef(indexSource, statement.loc);
                this->emit(std::move(index));
            }
        };

        if (shape == IterableShape::Counter) {
            const BlockId ascendingBlock = this->newBlock();
            const BlockId descendingBlock = this->newBlock();
            const BlockId directionJoin = this->newBlock();
            loopBlock = this->newBlock();
            bodyBlock = this->newBlock();
            exitBlock = this->newBlock();

            const hir::Expr& range = this->hirBody->exprs[statement.value];
            if (range.kind != hir::ExprKind::Range || range.args.size() < 2) {
                this->diagnostics.addError(statement.loc, "Invalid range iterable");
                return;
            }

            const LocalId cursor = this->addTemp(this->program.types->primitive(ty::TyKind::Int));
            const LocalId limit = this->addTemp(this->program.types->primitive(ty::TyKind::Int));
            const LocalId direction = this->addTemp(this->program.types->primitive(ty::TyKind::Int));

            storeState(cursor, this->lowerValue(range.args[0]));
            storeState(limit, this->lowerValue(range.args[1]));

            const ExprId ascending = this->makeCompare("<=",
                this->localRef(cursor, statement.loc), this->localRef(limit, statement.loc), statement.loc);
            this->terminate({ TerminatorKind::Branch, statement.loc, ascendingBlock, descendingBlock, ascending });

            this->selectBlock(ascendingBlock);
            storeState(direction, this->literal(1, this->program.types->primitive(ty::TyKind::Int), statement.loc));
            this->terminate({ TerminatorKind::Goto, statement.loc, directionJoin });

            this->selectBlock(descendingBlock);
            storeState(direction, this->literal(-1, this->program.types->primitive(ty::TyKind::Int), statement.loc));
            this->terminate({ TerminatorKind::Goto, statement.loc, directionJoin });

            this->selectBlock(directionJoin);
            this->terminate({ TerminatorKind::Goto, statement.loc, loopBlock });
            this->selectBlock(loopBlock);

            const ExprId delta = this->makeBinary("-",
                this->localRef(cursor, statement.loc), this->localRef(limit, statement.loc),
                this->program.types->primitive(ty::TyKind::Int), statement.loc);
            const ExprId scaled = this->makeBinary("*", delta,
                this->localRef(direction, statement.loc),
                this->program.types->primitive(ty::TyKind::Int), statement.loc);
            const ExprId condition = this->makeCompare("<", scaled,
                this->literal(0, this->program.types->primitive(ty::TyKind::Int), statement.loc), statement.loc);
            this->terminate({ TerminatorKind::Branch, statement.loc, bodyBlock, exitBlock, condition });

            this->selectBlock(bodyBlock);
            storeElement(this->localRef(cursor, statement.loc), cursor);

            const ExprId next = this->makeBinary("+",
                this->localRef(cursor, statement.loc), this->localRef(direction, statement.loc),
                this->program.types->primitive(ty::TyKind::Int), statement.loc);
            storeState(cursor, next);

            this->loops.push_back({ exitBlock, loopBlock });
            this->lowerBlock(statement.body);
            this->loops.pop_back();
            if (!this->finished)
                this->terminate({ TerminatorKind::Goto, statement.loc, loopBlock });
        } else if (shape == IterableShape::Lazy) {
            loopBlock = this->newBlock();
            bodyBlock = this->newBlock();
            exitBlock = this->newBlock();

            const LocalId sequence = this->addTemp(this->program.types->dynamic());
            const LocalId current = this->addTemp(this->program.types->primitive(ty::TyKind::Int));
            const LocalId next = this->addTemp(this->program.types->dynamic());

            storeState(sequence, this->lowerValue(statement.value));
            storeState(current, this->literal(0, this->program.types->primitive(ty::TyKind::Int), statement.loc));
            this->terminate({ TerminatorKind::Goto, statement.loc, loopBlock });
            this->selectBlock(loopBlock);

            const ExprId nextCall = this->makeByNameCall("next",
                { this->localRef(sequence, statement.loc) }, this->program.types->dynamic(), statement.loc);
            this->body->exprs[nextCall].flag2 = true;
            storeState(next, nextCall);

            const ExprId done = this->makeIsNone(this->localRef(next, statement.loc), statement.loc);
            this->terminate({ TerminatorKind::Branch, statement.loc, exitBlock, bodyBlock, done });

            this->selectBlock(bodyBlock);
            const ExprId elementValue = this->makeUnwrap(this->localRef(next, statement.loc), elementType, statement.loc);
            this->body->exprs[elementValue].flag2 = true;
            storeElement(elementValue, current);
            storeState(current, this->makeBinary("+",
                this->localRef(current, statement.loc),
                this->literal(1, this->program.types->primitive(ty::TyKind::Int), statement.loc),
                this->program.types->primitive(ty::TyKind::Int), statement.loc));

            this->loops.push_back({ exitBlock, loopBlock });
            this->lowerBlock(statement.body);
            this->loops.pop_back();
            if (!this->finished)
                this->terminate({ TerminatorKind::Goto, statement.loc, loopBlock });
        } else {
            loopBlock = this->newBlock();
            bodyBlock = this->newBlock();
            exitBlock = this->newBlock();

            const LocalId sequence = this->addTemp(this->program.types->dynamic());
            const LocalId current = this->addTemp(this->program.types->primitive(ty::TyKind::Int));

            storeState(sequence, this->lowerValue(statement.value));
            storeState(current, this->literal(0, this->program.types->primitive(ty::TyKind::Int), statement.loc));
            this->terminate({ TerminatorKind::Goto, statement.loc, loopBlock });
            this->selectBlock(loopBlock);

            ExprId length = hir::invalidId;
            if (shape == IterableShape::Convention &&
                ClassCall::getInstance().isRegistered(statement.className)) {
                length = this->makeNativeCall(statement.className, "length",
                    { this->localRef(sequence, statement.loc) }, false,
                    this->program.types->primitive(ty::TyKind::Int), statement.loc);
            } else if (shape == IterableShape::Convention) {
                length = this->makeByNameCall("length",
                    { this->localRef(sequence, statement.loc) },
                    this->program.types->primitive(ty::TyKind::Int), statement.loc);
            } else {
                length = this->makeLen(this->localRef(sequence, statement.loc), statement.loc);
            }

            const ExprId condition = this->makeCompare("<",
                this->localRef(current, statement.loc), length, statement.loc);
            this->terminate({ TerminatorKind::Branch, statement.loc, bodyBlock, exitBlock, condition });

            this->selectBlock(bodyBlock);

            ExprId elementValue = hir::invalidId;
            if (shape == IterableShape::Convention &&
                ClassCall::getInstance().isRegistered(statement.className)) {
                elementValue = this->makeNativeCall(statement.className, "element",
                    { this->localRef(current, statement.loc), this->localRef(sequence, statement.loc) },
                    false, elementType, statement.loc);
            } else if (shape == IterableShape::Convention) {
                elementValue = this->makeByNameCall("element",
                    { this->localRef(current, statement.loc), this->localRef(sequence, statement.loc) },
                    elementType, statement.loc);
            } else {
                elementValue = this->makeIndex(this->localRef(sequence, statement.loc),
                    this->localRef(current, statement.loc), elementType, statement.loc);
            }

            this->body->exprs[elementValue].flag2 = true;
            storeElement(elementValue, current);
            storeState(current, this->makeBinary("+",
                this->localRef(current, statement.loc),
                this->literal(1, this->program.types->primitive(ty::TyKind::Int), statement.loc),
                this->program.types->primitive(ty::TyKind::Int), statement.loc));

            this->loops.push_back({ exitBlock, loopBlock });
            this->lowerBlock(statement.body);
            this->loops.pop_back();
            if (!this->finished)
                this->terminate({ TerminatorKind::Goto, statement.loc, loopBlock });
        }

        this->selectBlock(exitBlock);
    }

    void MirBuilder::buildBody(Body& target, const hir::Body& source) {
        this->body = &target;
        this->hirBody = &source;
        this->body->exprs.clear();
        this->body->blocks.clear();
        this->loops.clear();
        this->currentBlock = this->newBlock();
        this->finished = false;

        if (source.isTopLevel) {
            const ExprId value = this->lowerBlockValue(source.root, true);
            if (!this->finished)
                this->terminate({ TerminatorKind::Halt, {}, invalidId, invalidId, invalidId, value });
            return;
        }

        const ExprId value = this->lowerBlockValue(source.root, source.returnsValue);
        if (!this->finished)
            this->terminate({ TerminatorKind::Return, {}, invalidId, invalidId, invalidId, value });
    }
}
