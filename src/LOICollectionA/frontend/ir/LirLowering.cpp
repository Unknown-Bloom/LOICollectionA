#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "LOICollectionA/frontend/ir/LirLowering.h"

namespace LOICollection::frontend::ir {
    LirLowering::LirLowering(const hir::Program& source, const mir::Module& mir, DiagnosticEngine& diag)
        : program(source), module(mir), diagnostics(diag) {}

    Chunk LirLowering::lower() {
        this->buildMetadata();

        this->lowerBody(this->module.bodies[0], this->root, true);

        for (std::size_t index = 1; index < this->module.bodies.size(); ++index) {
            auto chunk = std::make_unique<Chunk>();
            this->lowerBody(this->module.bodies[index], *chunk, false);
            this->root.methodBodies.push_back(std::move(chunk));
        }

        return std::move(this->root);
    }

    int LirLowering::slotOf(const mir::Body& target, hir::LocalId local) const {
        const int base = target.isTopLevel ? 1 : 0;
        return base + static_cast<int>(local);
    }

    int LirLowering::allocReg() {
        return this->nextReg++;
    }

    int LirLowering::reserveRegs(int count) {
        const int base = this->nextReg;
        this->nextReg += count;
        return base;
    }

    int LirLowering::addConstant(const ValueNode::ValueType& value) {
        this->body->constants.push_back(value);
        return static_cast<int>(this->body->constants.size() - 1);
    }

    int LirLowering::addFunction(const std::string& name, int argCount) {
        this->body->functions.push_back({ name, argCount });
        return static_cast<int>(this->body->functions.size() - 1);
    }

    int LirLowering::addMacro(const std::string& name, int argCount) {
        this->body->macros.push_back({ name, argCount });
        return static_cast<int>(this->body->macros.size() - 1);
    }

    int LirLowering::addNativeCall(const std::string& className, const std::string& name, int argCount, bool isStatic) {
        for (std::size_t index = 0; index < this->root.nativeCalls.size(); ++index) {
            const NativeCallMeta& meta = this->root.nativeCalls[index];
            if (meta.className == className && meta.name == name &&
                meta.argCount == argCount && meta.isStatic == isStatic)
                return static_cast<int>(index);
        }

        this->root.nativeCalls.push_back({ className, name, argCount, isStatic });
        return static_cast<int>(this->root.nativeCalls.size() - 1);
    }

    int LirLowering::addVirtualCall(int classIndex, int ordinal, int argCount) {
        this->body->virtualCalls.push_back({ classIndex, ordinal, argCount });
        return static_cast<int>(this->body->virtualCalls.size() - 1);
    }

    int LirLowering::addByNameCall(const std::string& name, int argCount) {
        this->body->byNameCalls.push_back({ name, argCount });
        return static_cast<int>(this->body->byNameCalls.size() - 1);
    }

    int LirLowering::addSuperCall(int constructorIndex, int argCount) {
        this->body->superCalls.push_back({ constructorIndex, argCount });
        return static_cast<int>(this->body->superCalls.size() - 1);
    }

    int LirLowering::addLambda(int bodyIndex, int argCount, const std::vector<LambdaCapture>& captures) {
        LambdaMeta meta;
        meta.bodyIndex = bodyIndex;
        meta.argCount = argCount;
        meta.captureCount = static_cast<int>(captures.size());
        meta.captures = captures;
        this->body->lambdas.push_back(std::move(meta));
        return static_cast<int>(this->body->lambdas.size() - 1);
    }

    TypeInfo LirLowering::legacyType(ty::Ty type) const {
        return this->program.types->raise(type);
    }

    void LirLowering::buildMetadata() {
        for (const hir::Class& cls : this->program.classes) {
            ClassMeta meta;
            meta.name = cls.name;

            auto base = this->program.classIndices.find(cls.baseName);
            meta.baseClassIndex = base == this->program.classIndices.end() ? -1 : base->second;

            for (const ty::FieldDef& field : cls.fields) {
                meta.fieldNames.push_back(field.name);
                meta.defaults.push_back(field.defaultValue);
                meta.hasDefault.push_back(field.hasDefault);
            }

            for (const ty::FieldDef& field : cls.staticFields) {
                meta.staticFieldNames.push_back(field.name);
                meta.staticDefaults.push_back(field.defaultValue);
                meta.staticHasDefault.push_back(field.hasDefault);
            }

            meta.constructorIndex = cls.constructorOrdinal;
            meta.methods = cls.methodTable;
            meta.methodSignatures = cls.methodSignatures;
            meta.staticMethods = cls.staticMethodTable;
            meta.staticMethodSignatures = cls.staticMethodSignatures;
            meta.ancestorIndices = cls.ancestors;
            meta.ancestorMask = cls.ancestorMask;

            this->root.classes.push_back(std::move(meta));
        }

        for (const hir::Enum& enumeration : this->program.enums) {
            EnumMeta meta;
            meta.name = enumeration.name;

            for (const hir::EnumVariant& variant : enumeration.variants) {
                meta.variantNames.push_back(variant.name);
                meta.variantArity.push_back(static_cast<std::uint8_t>(variant.payload.size()));
            }

            this->root.enums.push_back(std::move(meta));
        }

        std::vector<MethodMeta> methods(this->program.bodies.size());
        std::vector<bool> present(this->program.bodies.size(), false);

        for (const hir::Class& cls : this->program.classes) {
            for (const hir::Method& method : cls.methods) {
                if (method.bodyIndex == hir::invalidId || method.bodyIndex >= methods.size())
                    continue;

                methods[method.bodyIndex] = MethodMeta{
                    method.name,
                    static_cast<int>(method.params.size()),
                    cls.classIndex,
                    static_cast<int>(method.bodyIndex)
                };
                present[method.bodyIndex] = true;
            }
        }

        for (const hir::Function& function : this->program.functions) {
            if (function.bodyIndex == hir::invalidId || function.bodyIndex >= methods.size())
                continue;

            methods[function.bodyIndex] = MethodMeta{
                function.name,
                static_cast<int>(function.params.size()),
                -1,
                static_cast<int>(function.bodyIndex)
            };
            present[function.bodyIndex] = true;
        }

        for (std::size_t index = 0; index < methods.size(); ++index) {
            if (present[index])
                this->root.methods.push_back(std::move(methods[index]));
        }
    }

    void LirLowering::lowerBody(const mir::Body& source, Chunk& target, bool topLevel) {
        this->body = &target;
        this->slotBase = topLevel ? 1 : 0;
        this->nextReg = this->slotBase + static_cast<int>(source.locals.size());
        this->jumps.clear();
        this->blockStarts.assign(source.blocks.size(), -1);

        for (std::size_t index = 0; index < source.blocks.size(); ++index) {
            this->blockStarts[index] = static_cast<int>(this->body->code.size());
            this->lowerBlock(source, source.blocks[index]);
        }

        for (const JumpPatch& jump : this->jumps) {
            const int targetIndex = this->blockStarts[jump.target];
            this->body->code[jump.index].operand =
                targetIndex - static_cast<int>(jump.index) - 1;
        }

        this->body->slotCount = this->nextReg;
    }

    void LirLowering::lowerBlock(const mir::Body& source, const mir::Block& block) {
        for (const mir::Statement& statement : block.statements)
            this->lowerStatement(source, statement);

        this->lowerTerminator(source, block.terminator);
    }

    void LirLowering::lowerStatement(const mir::Body& source, const mir::Statement& statement) {
        switch (statement.kind) {
            case mir::StatementKind::Evaluate:
                static_cast<void>(this->compileValue(source, statement.expr));
                break;
            case mir::StatementKind::StoreLocal: {
                const int value = this->compileValue(source, statement.expr);
                this->storeLocal(statement.local, source, value);
                break;
            }
            case mir::StatementKind::StoreGlobal: {
                const int value = this->compileValue(source, statement.expr);
                this->storeVariable(statement.name, value);
                break;
            }
            case mir::StatementKind::StoreField: {
                const int value = this->compileValue(source, statement.expr);
                this->storeField(source, source.exprs[statement.target], value);
                break;
            }
            case mir::StatementKind::StoreIndex: {
                const int value = this->compileValue(source, statement.expr);
                this->storeIndex(source, source.exprs[statement.target], value);
                break;
            }
            case mir::StatementKind::CompoundAssign:
                static_cast<void>(this->compileCompoundAssign(
                    source, statement.expr, statement.target, statement.op, statement.loc
                ));
                break;
        }
    }

    void LirLowering::lowerTerminator(const mir::Body& source, const mir::Terminator& terminator) {
        switch (terminator.kind) {
            case mir::TerminatorKind::Unreachable: {
                this->body->emit(Op::HALT);
                break;
            }
            case mir::TerminatorKind::Goto: {
                const std::size_t index = this->body->emit(Op::JMP, 0, terminator.loc);
                this->jumps.push_back({ index, terminator.target });
                break;
            }
            case mir::TerminatorKind::Branch: {
                const int condition = this->compileValue(source, terminator.condition);
                const std::size_t skip = this->body->emit(Op::JMP_IF_FALSE, 0, -1, condition, -1, terminator.loc);
                this->jumps.push_back({ skip, terminator.alternate });
                const std::size_t jump = this->body->emit(Op::JMP, 0, terminator.loc);
                this->jumps.push_back({ jump, terminator.target });
                break;
            }
            case mir::TerminatorKind::SwitchTag: {
                const int value = this->compileValue(source, terminator.value);
                const std::size_t skip = this->body->emitCall(
                    Op::JMP_IF_TAG_NE, 0, -1, value, terminator.tagEnum, terminator.tag, terminator.loc
                );
                this->jumps.push_back({ skip, terminator.alternate });
                const std::size_t jump = this->body->emit(Op::JMP, 0, terminator.loc);
                this->jumps.push_back({ jump, terminator.target });
                break;
            }
            case mir::TerminatorKind::Return: {
                const int value = terminator.value == mir::invalidId
                    ? -1
                    : this->compileValue(source, terminator.value);
                this->body->emit(Op::RETURN, 0, -1, value, -1, terminator.loc);
                break;
            }
            case mir::TerminatorKind::Halt: {
                const int value = terminator.value == mir::invalidId
                    ? this->addConstant(std::string(""))
                    : this->compileValue(source, terminator.value);
                if (value != 0)
                    this->body->emit(Op::MOVE, 0, 0, value, -1, terminator.loc);
                this->body->emit(Op::HALT);
                break;
            }
            case mir::TerminatorKind::Panic: {
                const int value = terminator.value == mir::invalidId
                    ? -1
                    : this->compileValue(source, terminator.value);
                this->body->emit(Op::PANIC, 0, -1, value, -1, terminator.loc);
                break;
            }
        }
    }

    void LirLowering::storeLocal(hir::LocalId local, const mir::Body& source, int valueReg) {
        this->body->emit(Op::STORE_SLOT, this->slotOf(source, local), -1, valueReg, -1);
    }

    void LirLowering::storeVariable(const std::string& name, int valueReg) {
        this->body->emit(Op::STORE_VAR, this->addConstant(name), -1, valueReg, -1);
    }

    void LirLowering::storeField(const mir::Body& source, const hir::Expr& target, int valueReg) {
        const int object = this->compileValue(source, target.args[0]);
        if (target.slot >= 0) {
            this->body->emit(Op::STORE_FIELD_SLOT, target.slot, -1, object, valueReg, target.loc);
            return;
        }
        this->body->emit(Op::STORE_FIELD, this->addConstant(target.name), -1, object, valueReg, target.loc);
    }

    void LirLowering::storeIndex(const mir::Body& source, const hir::Expr& target, int valueReg) {
        const int object = this->compileValue(source, target.args[0]);
        const int index = this->compileValue(source, target.args[1]);
        this->body->emit(Op::STORE_INDEX, 0, -1, object, index, valueReg, target.loc);
    }

    void LirLowering::fail(const SourceLocation& loc, const std::string& message) {
        this->diagnostics.addError(loc, message);
    }

    int LirLowering::compileValue(const mir::Body& source, mir::ExprId id) {
        if (id == mir::invalidId || id >= source.exprs.size())
            return -1;

        const hir::Expr& value = source.exprs[id];
        int reg = this->compileOperand(source, value);

        if (reg < 0)
            return reg;

        if (value.type.valid() &&
            this->program.types->kind(value.type) == ty::TyKind::Optional &&
            !value.flag2) {
            const int dst = this->allocReg();
            this->body->emit(Op::UNWRAP, 0, dst, reg, -1, value.loc, this->legacyType(value.type));
            reg = dst;
        }

        return reg;
    }

    int LirLowering::compileInto(const mir::Body& source, int dst, mir::ExprId id) {
        if (dst < 0)
            return this->compileValue(source, id);

        if (auto leaf = this->tryEmitLeaf(source, dst, id))
            return *leaf;

        const int reg = this->compileValue(source, id);
        if (reg < 0)
            return reg;

        if (reg != dst)
            this->body->emit(Op::MOVE, 0, dst, reg, -1, source.exprs[id].loc);

        return dst;
    }

    std::optional<int> LirLowering::tryEmitLeaf(const mir::Body& source, int dst, mir::ExprId id) {
        if (id == mir::invalidId || id >= source.exprs.size())
            return std::nullopt;

        const hir::Expr& value = source.exprs[id];
        if (value.type.valid() &&
            this->program.types->kind(value.type) == ty::TyKind::Optional &&
            !value.flag2)
            return std::nullopt;

        switch (value.kind) {
            case hir::ExprKind::Literal:
                this->body->emit(Op::LOAD_CONST, this->addConstant(value.literal), dst, -1, -1, value.loc);
                return dst;
            case hir::ExprKind::LocalRef:
                this->body->emit(Op::LOAD_SLOT, this->slotOf(source, static_cast<hir::LocalId>(value.ordinal)),
                    dst, -1, -1, value.loc);
                return dst;
            case hir::ExprKind::GlobalRef:
                this->body->emit(Op::LOAD_VAR, this->addConstant(value.name), dst, -1, -1, value.loc);
                return dst;
            case hir::ExprKind::This:
            case hir::ExprKind::Super:
                this->body->emit(Op::LOAD_THIS, 0, dst, -1, -1, value.loc);
                return dst;
            default:
                return std::nullopt;
        }
    }

    int LirLowering::compileArgs(const mir::Body& source, const std::vector<mir::ExprId>& args) {
        const int base = this->reserveRegs(static_cast<int>(args.size()));
        for (std::size_t index = 0; index < args.size(); ++index)
            static_cast<void>(this->compileInto(source, base + static_cast<int>(index), args[index]));
        return base;
    }

    int LirLowering::compileOperand(const mir::Body& source, const hir::Expr& value) {
        switch (value.kind) {
            case hir::ExprKind::Literal:
                return this->compileLiteral(value);
            case hir::ExprKind::LocalRef:
                return this->compileLocal(source, value);
            case hir::ExprKind::GlobalRef:
                return this->compileGlobal(value);
            case hir::ExprKind::This:
            case hir::ExprKind::Super: {
                const int dst = this->allocReg();
                this->body->emit(Op::LOAD_THIS, 0, dst, -1, -1, value.loc);
                return dst;
            }
            case hir::ExprKind::Field:
            case hir::ExprKind::SafeField:
                return this->compileField(source, value);
            case hir::ExprKind::Index:
            case hir::ExprKind::SafeIndex:
                return this->compileIndex(source, value);
            case hir::ExprKind::Call:
            case hir::ExprKind::CallHost:
            case hir::ExprKind::CallMacro:
            case hir::ExprKind::CallLambda:
                return this->compileCall(source, value);
            case hir::ExprKind::MethodCall:
            case hir::ExprKind::MethodVirtual:
            case hir::ExprKind::MethodByName:
            case hir::ExprKind::NativeCall:
            case hir::ExprKind::SuperCall:
                return this->compileMethod(source, value);
            case hir::ExprKind::New:
            case hir::ExprKind::NewNative:
                return this->compileNew(source, value);
            case hir::ExprKind::Lambda:
                return this->compileLambda(source, value);
            case hir::ExprKind::Array:
                return this->compileArray(source, value);
            case hir::ExprKind::Binary:
            case hir::ExprKind::Logical:
                return this->compileBinaryExpr(source, value);
            case hir::ExprKind::Unary:
                return this->compileUnaryExpr(source, value);
            case hir::ExprKind::Compare:
                return this->compileCompareExpr(source, value);
            case hir::ExprKind::TypeOf:
            case hir::ExprKind::HasValue:
            case hir::ExprKind::IsNone:
            case hir::ExprKind::Unwrap:
            case hir::ExprKind::Len:
                return this->compileOptional(source, value);
            case hir::ExprKind::InstanceOf: {
                const int target = this->compileValue(source, value.args[0]);
                const int dst = this->allocReg();
                this->body->emit(Op::INSTANCEOF, this->addConstant(value.className), dst, target, -1, value.loc);
                return dst;
            }
            case hir::ExprKind::EnumConstruct:
            case hir::ExprKind::EnumPayload:
                return this->compileEnum(source, value);
            case hir::ExprKind::Assign:
                return this->compileAssignExpr(source, value);
            case hir::ExprKind::CompoundAssign:
                return this->compileCompoundAssignExpr(source, value);
            default:
                this->fail(value.loc, "Unlowered MIR expression");
                return -1;
        }
    }

    int LirLowering::compileLiteral(const hir::Expr& expr) {
        const int dst = this->allocReg();
        this->body->emit(Op::LOAD_CONST, this->addConstant(expr.literal), dst, -1, -1, expr.loc);
        return dst;
    }

    int LirLowering::compileLocal(const mir::Body& source, const hir::Expr& expr) {
        const int dst = this->allocReg();
        this->body->emit(Op::LOAD_SLOT, this->slotOf(source, static_cast<hir::LocalId>(expr.ordinal)),
            dst, -1, -1, expr.loc);
        return dst;
    }

    int LirLowering::compileGlobal(const hir::Expr& expr) {
        const int dst = this->allocReg();
        this->body->emit(Op::LOAD_VAR, this->addConstant(expr.name), dst, -1, -1, expr.loc);
        return dst;
    }

    int LirLowering::compileField(const mir::Body& source, const hir::Expr& expr) {
        const int object = this->compileValue(source, expr.args[0]);
        const int dst = this->allocReg();

        if (expr.slot >= 0) {
            this->body->emit(Op::LOAD_FIELD_SLOT, expr.slot, dst, object, -1, expr.loc);
            return dst;
        }

        this->body->emit(Op::LOAD_FIELD, this->addConstant(expr.name), dst, object, -1, expr.loc);
        return dst;
    }

    int LirLowering::compileIndex(const mir::Body& source, const hir::Expr& expr) {
        const int object = this->compileValue(source, expr.args[0]);
        const int index = this->compileValue(source, expr.args[1]);
        const int dst = this->allocReg();
        this->body->emit(Op::LOAD_INDEX, 0, dst, object, index, expr.loc);
        return dst;
    }

    int LirLowering::compileCall(const mir::Body& source, const hir::Expr& expr) {
        const int argCount = static_cast<int>(expr.args.size());
        const int base = this->compileArgs(source, expr.args);

        if (expr.kind == hir::ExprKind::CallHost) {
            const int meta = this->addFunction(expr.name, argCount);
            const int dst = this->allocReg();
            this->body->emitCall(Op::CALL, meta, dst, base, -1, argCount, expr.loc);
            return dst;
        }

        if (expr.kind == hir::ExprKind::CallMacro) {
            const int meta = this->addMacro(expr.name, argCount);
            const int dst = this->allocReg();
            this->body->emitCall(Op::CALL_MACRO, meta, dst, base, -1, argCount, expr.loc);
            return dst;
        }

        if (expr.kind == hir::ExprKind::CallLambda) {
            int functionReg = -1;
            for (std::size_t index = source.locals.size(); index > 0; --index) {
                if (source.locals[index - 1].name != expr.name)
                    continue;

                functionReg = this->allocReg();
                this->body->emit(Op::LOAD_SLOT, this->slotOf(source, static_cast<hir::LocalId>(index - 1)),
                    functionReg, -1, -1, expr.loc);
                break;
            }

            if (functionReg < 0) {
                functionReg = this->allocReg();
                this->body->emit(Op::LOAD_VAR, this->addConstant(expr.name), functionReg, -1, -1, expr.loc);
            }

            const int dst = this->allocReg();
            this->body->emitCall(Op::CALL_LAMBDA, argCount, dst, base, functionReg, argCount, expr.loc);
            return dst;
        }

        int ordinal = expr.ordinal;
        if (expr.flag) {
            auto classIt = this->program.classIndices.find(expr.className);
            if (classIt == this->program.classIndices.end()) {
                this->fail(expr.loc, "Unresolved static method call: " + expr.name);
                return -1;
            }

            const hir::Class& cls = this->program.classes[static_cast<std::size_t>(classIt->second)];
            ordinal = expr.ordinal >= 0 && static_cast<std::size_t>(expr.ordinal) < cls.staticMethodTable.size()
                ? cls.staticMethodTable[static_cast<std::size_t>(expr.ordinal)]
                : -1;
        }

        if (ordinal < 0) {
            this->fail(expr.loc, "Unresolved function call: " + expr.name);
            return -1;
        }

        const int dst = this->allocReg();
        this->body->emitCall(Op::CALL_FUNC, ordinal, dst, base, -1, argCount, expr.loc);
        return dst;
    }

    int LirLowering::compileMethod(const mir::Body& source, const hir::Expr& expr) {
        if (expr.kind == hir::ExprKind::SuperCall) {
            const int argCount = static_cast<int>(expr.args.size());
            const int base = this->reserveRegs(argCount + 1);
            for (std::size_t index = 0; index < expr.args.size(); ++index)
                static_cast<void>(this->compileInto(source, base + static_cast<int>(index), expr.args[index]));

            this->body->emit(Op::LOAD_THIS, 0, base + argCount, -1, -1, expr.loc);
            const int meta = this->addSuperCall(expr.ordinal, argCount);
            this->body->emitCall(Op::CALL_SUPER_CTOR, meta, -1, base, -1, argCount, expr.loc);
            return -1;
        }

        if (expr.kind == hir::ExprKind::Call) {
            return this->compileCall(source, expr);
        }

        const bool isStatic = expr.flag && expr.kind == hir::ExprKind::NativeCall;
        const int argCount = static_cast<int>(expr.args.size()) - (isStatic ? 0 : 1);
        const int base = this->compileArgs(source, expr.args);
        const int receiver = isStatic ? -1 : base + argCount;

        if (!isStatic && expr.bindsReceiver)
            this->bindThis(source, base, argCount, receiver, expr.args);

        if (expr.kind == hir::ExprKind::NativeCall) {
            const int meta = this->addNativeCall(expr.className, expr.name, argCount, isStatic);
            const int dst = this->allocReg();
            this->body->emitCall(Op::CALL_NATIVE_METHOD, meta, dst, base, -1, argCount, expr.loc);
            return dst;
        }

        if (expr.kind == hir::ExprKind::MethodByName) {
            const int meta = this->addByNameCall(expr.name, argCount);
            const int dst = this->allocReg();
            this->body->emitCall(Op::CALL_METHOD_BY_NAME, meta, dst, base, -1, argCount, expr.loc);
            return dst;
        }

        if (expr.kind == hir::ExprKind::MethodVirtual) {
            auto classIt = this->program.classIndices.find(expr.className);
            if (classIt == this->program.classIndices.end()) {
                this->fail(expr.loc, "Unresolved method call: " + expr.name);
                return -1;
            }

            const int meta = this->addVirtualCall(classIt->second, expr.ordinal, argCount);
            const int dst = this->allocReg();
            this->body->emitCall(Op::CALL_METHOD_VIRTUAL, meta, dst, base, -1, argCount, expr.loc);
            return dst;
        }

        auto classIt = this->program.classIndices.find(expr.className);
        if (classIt == this->program.classIndices.end()) {
            this->fail(expr.loc, "Unresolved method call: " + expr.name);
            return -1;
        }

        const hir::Class& cls = this->program.classes[static_cast<std::size_t>(classIt->second)];
        const int ordinal = expr.ordinal >= 0 && static_cast<std::size_t>(expr.ordinal) < cls.methodTable.size()
            ? cls.methodTable[static_cast<std::size_t>(expr.ordinal)]
            : -1;

        if (ordinal < 0) {
            this->fail(expr.loc, "Unresolved method call: " + expr.name);
            return -1;
        }

        const int dst = this->allocReg();
        this->body->emitCall(Op::CALL_METHOD, ordinal, dst, base, -1, argCount, expr.loc);
        return dst;
    }

    int LirLowering::compileNew(const mir::Body& source, const hir::Expr& expr) {
        const int argCount = static_cast<int>(expr.args.size());
        const int base = this->compileArgs(source, expr.args);
        const int dst = this->allocReg();

        if (expr.kind == hir::ExprKind::NewNative) {
            const int meta = this->addNativeCall(expr.className, "", argCount, false);
            this->body->emitCall(Op::NEW_NATIVE, meta, dst, base, -1, argCount, expr.loc);
            return dst;
        }

        this->body->emitCall(Op::NEW, expr.ordinal, dst, base, -1, argCount, expr.loc);
        return dst;
    }

    int LirLowering::compileLambda(const mir::Body& source, const hir::Expr& expr) {
        std::vector<LambdaCapture> captures;
        captures.reserve(expr.captures.size());

        for (const hir::Capture& capture : expr.captures) {
            captures.push_back({
                capture.name,
                this->slotOf(source, capture.source),
                static_cast<int>(capture.target)
            });
        }

        const int meta = this->addLambda(static_cast<int>(expr.bodyIndex), expr.arity, captures);
        const int dst = this->allocReg();
        this->body->emitCall(Op::MAKE_LAMBDA, meta, dst, -1, -1, static_cast<int>(captures.size()), expr.loc);
        return dst;
    }

    int LirLowering::compileArray(const mir::Body& source, const hir::Expr& expr) {
        const int count = static_cast<int>(expr.args.size());
        const int base = this->compileArgs(source, expr.args);
        const int dst = this->allocReg();
        this->body->emitCall(Op::MAKE_ARRAY, count, dst, base, -1, count, expr.loc);
        return dst;
    }

    int LirLowering::compileBinaryExpr(const mir::Body& source, const hir::Expr& expr) {
        const int left = this->compileValue(source, expr.args[0]);
        const int right = this->compileValue(source, expr.args[1]);

        if (expr.op == "&&") {
            const int dst = this->allocReg();
            this->body->emit(Op::LOGIC_AND, 0, dst, left, right, expr.loc);
            return dst;
        }

        if (expr.op == "||") {
            const int dst = this->allocReg();
            this->body->emit(Op::LOGIC_OR, 0, dst, left, right, expr.loc);
            return dst;
        }

        return this->emitArithmetic(expr.op, source.exprs[expr.args[0]].type,
            source.exprs[expr.args[1]].type, left, right, expr.loc);
    }

    int LirLowering::compileUnaryExpr(const mir::Body& source, const hir::Expr& expr) {
        const int operand = this->compileValue(source, expr.args[0]);

        if (expr.op == "-") {
            const int dst = this->allocReg();
            this->body->emit(Op::NEG, 0, dst, operand, -1, expr.loc, this->legacyType(expr.type));
            return dst;
        }

        if (expr.op == "!") {
            const int dst = this->allocReg();
            this->body->emit(Op::NOT, 0, dst, operand, -1, expr.loc);
            return dst;
        }

        return operand;
    }

    int LirLowering::compileCompareExpr(const mir::Body& source, const hir::Expr& expr) {
        const int left = this->compileValue(source, expr.args[0]);
        const int right = this->compileValue(source, expr.args[1]);
        return this->emitCompare(expr.op, left, right, expr.loc);
    }

    int LirLowering::compileOptional(const mir::Body& source, const hir::Expr& expr) {
        const int operand = expr.args.empty()
            ? -1
            : this->compileOperand(source, source.exprs[expr.args[0]]);
        const int dst = this->allocReg();

        switch (expr.kind) {
            case hir::ExprKind::Unwrap:
                this->body->emit(Op::UNWRAP, 0, dst, operand, -1, expr.loc, this->legacyType(expr.type));
                break;
            case hir::ExprKind::TypeOf:
                this->body->emit(Op::TYPE_OF, 0, dst, operand, -1, expr.loc);
                break;
            case hir::ExprKind::HasValue:
                this->body->emit(Op::HAS_VALUE, 0, dst, operand, -1, expr.loc);
                break;
            case hir::ExprKind::IsNone:
                this->body->emit(Op::IS_NONE, 0, dst, operand, -1, expr.loc);
                break;
            case hir::ExprKind::Len:
                this->body->emit(Op::LOAD_LEN, 0, dst, operand, -1, expr.loc);
                break;
            default:
                break;
        }

        return dst;
    }

    int LirLowering::compileEnum(const mir::Body& source, const hir::Expr& expr) {
        if (expr.kind == hir::ExprKind::EnumPayload) {
            const int value = this->compileValue(source, expr.args[0]);
            const int dst = this->allocReg();
            this->body->emit(Op::LOAD_ENUM_PAYLOAD, expr.slot, dst, value, -1, expr.loc);
            return dst;
        }

        const int arity = static_cast<int>(expr.args.size());
        const int base = this->compileArgs(source, expr.args);
        const int dst = this->allocReg();

        const std::size_t index = this->body->emit(
            Op::MAKE_ENUM, expr.ordinal, dst,
            arity >= 1 ? base : -1,
            arity >= 2 ? base + 1 : -1,
            arity >= 3 ? base + 2 : -1,
            expr.loc
        );
        this->body->code[index].imm = expr.tag;

        return dst;
    }

    int LirLowering::compileAssignExpr(const mir::Body& source, const hir::Expr& expr) {
        const int value = this->compileValue(source, expr.args[0]);
        const hir::Expr& target = source.exprs[expr.args[1]];

        switch (target.kind) {
            case hir::ExprKind::LocalRef:
                this->storeLocal(static_cast<hir::LocalId>(target.ordinal), source, value);
                break;
            case hir::ExprKind::GlobalRef:
                this->storeVariable(target.name, value);
                break;
            case hir::ExprKind::Field:
            case hir::ExprKind::SafeField:
                this->storeField(source, target, value);
                break;
            case hir::ExprKind::Index:
            case hir::ExprKind::SafeIndex:
                this->storeIndex(source, target, value);
                break;
            default:
                this->fail(expr.loc, "Invalid assignment target");
                return -1;
        }

        return value;
    }

    int LirLowering::compileCompoundAssignExpr(const mir::Body& source, const hir::Expr& expr) {
        return this->compileCompoundAssign(source, expr.args[0], expr.args[1], expr.op, expr.loc);
    }

    int LirLowering::compileCompoundAssign(
        const mir::Body& source, mir::ExprId valueId, mir::ExprId targetId,
        const std::string& op, const SourceLocation& loc
    ) {
        const hir::Expr& target = source.exprs[targetId];
        const hir::Expr& value = source.exprs[valueId];

        if (target.kind == hir::ExprKind::LocalRef) {
            const int lhs = this->allocReg();
            this->body->emit(Op::LOAD_SLOT, this->slotOf(source, static_cast<hir::LocalId>(target.ordinal)),
                lhs, -1, -1, loc);

            int left = lhs;
            if (target.type.valid() &&
                this->program.types->kind(target.type) == ty::TyKind::Optional &&
                !target.flag2) {
                left = this->allocReg();
                this->body->emit(Op::UNWRAP, 0, left, lhs, -1, loc, this->legacyType(target.type));
            }

            const int right = this->compileValue(source, valueId);
            const int result = this->emitArithmetic(op, target.type, value.type, left, right, loc);
            this->body->emit(Op::STORE_SLOT, this->slotOf(source, static_cast<hir::LocalId>(target.ordinal)),
                -1, result, -1, loc);
            return result;
        }

        if (target.kind == hir::ExprKind::GlobalRef) {
            const int lhs = this->allocReg();
            this->body->emit(Op::LOAD_VAR, this->addConstant(target.name), lhs, -1, -1, loc);

            int left = lhs;
            if (target.type.valid() &&
                this->program.types->kind(target.type) == ty::TyKind::Optional &&
                !target.flag2) {
                left = this->allocReg();
                this->body->emit(Op::UNWRAP, 0, left, lhs, -1, loc, this->legacyType(target.type));
            }

            const int right = this->compileValue(source, valueId);
            const int result = this->emitArithmetic(op, target.type, value.type, left, right, loc);
            this->body->emit(Op::STORE_VAR, this->addConstant(target.name), -1, result, -1, loc);
            return result;
        }

        if (target.kind == hir::ExprKind::Field || target.kind == hir::ExprKind::SafeField) {
            const int object = this->compileValue(source, target.args[0]);
            const int lhs = this->allocReg();

            if (target.slot >= 0)
                this->body->emit(Op::LOAD_FIELD_SLOT, target.slot, lhs, object, -1, loc);
            else
                this->body->emit(Op::LOAD_FIELD, this->addConstant(target.name), lhs, object, -1, loc);

            int left = lhs;
            if (target.type.valid() &&
                this->program.types->kind(target.type) == ty::TyKind::Optional &&
                !target.flag2) {
                left = this->allocReg();
                this->body->emit(Op::UNWRAP, 0, left, lhs, -1, loc, this->legacyType(target.type));
            }

            const int right = this->compileValue(source, valueId);
            const int result = this->emitArithmetic(op, target.type, value.type, left, right, loc);

            if (target.slot >= 0)
                this->body->emit(Op::STORE_FIELD_SLOT, target.slot, -1, object, result, loc);
            else
                this->body->emit(Op::STORE_FIELD, this->addConstant(target.name), -1, object, result, loc);

            return result;
        }

        if (target.kind == hir::ExprKind::Index || target.kind == hir::ExprKind::SafeIndex) {
            const int object = this->compileValue(source, target.args[0]);
            const int index = this->compileValue(source, target.args[1]);
            const int lhs = this->allocReg();
            this->body->emit(Op::LOAD_INDEX, 0, lhs, object, index, loc);

            int left = lhs;
            if (target.type.valid() &&
                this->program.types->kind(target.type) == ty::TyKind::Optional &&
                !target.flag2) {
                left = this->allocReg();
                this->body->emit(Op::UNWRAP, 0, left, lhs, -1, loc, this->legacyType(target.type));
            }

            const int right = this->compileValue(source, valueId);
            const int result = this->emitArithmetic(op, target.type, value.type, left, right, loc);
            this->body->emit(Op::STORE_INDEX, 0, -1, object, index, result, loc);
            return result;
        }

        this->fail(loc, "Invalid compound assignment target");
        return -1;
    }

    int LirLowering::emitBinary(Op op, int left, int right, const SourceLocation& loc, const TypeInfo& type) {
        const int dst = this->allocReg();
        this->body->emit(op, 0, dst, left, right, loc, type);
        return dst;
    }

    int LirLowering::emitArithmetic(
        const std::string& op, const ty::Ty& leftType, const ty::Ty& rightType,
        int leftReg, int rightReg, const SourceLocation& loc
    ) {
        const bool isInt = leftType.valid() && rightType.valid() &&
            this->program.types->kind(leftType) == ty::TyKind::Int &&
            this->program.types->kind(rightType) == ty::TyKind::Int;
        const TypeInfo type = isInt ? this->legacyType(leftType) : TypeInfo{};

        if (op == "+")
            return this->emitBinary(Op::ADD, leftReg, rightReg, loc, type);
        if (op == "-")
            return this->emitBinary(Op::SUB, leftReg, rightReg, loc, type);
        if (op == "*")
            return this->emitBinary(Op::MUL, leftReg, rightReg, loc, type);
        if (op == "/")
            return this->emitBinary(Op::DIV, leftReg, rightReg, loc, type);
        if (op == "%")
            return this->emitBinary(Op::MOD, leftReg, rightReg, loc, type);
        if (op == "^")
            return this->emitBinary(Op::POW, leftReg, rightReg, loc, type);

        this->fail(loc, "Unknown arithmetic op: " + op);
        return leftReg;
    }

    int LirLowering::emitCompare(const std::string& op, int left, int right, const SourceLocation& loc) {
        if (op == "==")
            return this->emitBinary(Op::CMP_EQ, left, right, loc);
        if (op == "!=")
            return this->emitBinary(Op::CMP_NE, left, right, loc);
        if (op == ">")
            return this->emitBinary(Op::CMP_GT, left, right, loc);
        if (op == "<")
            return this->emitBinary(Op::CMP_LT, left, right, loc);
        if (op == ">=")
            return this->emitBinary(Op::CMP_GE, left, right, loc);
        if (op == "<=")
            return this->emitBinary(Op::CMP_LE, left, right, loc);

        this->fail(loc, "Unknown compare op: " + op);
        return left;
    }

    void LirLowering::bindThis(
        const mir::Body& source, int base, int argCount, int receiverReg,
        const std::vector<mir::ExprId>& args
    ) {
        for (int index = 0; index < argCount; ++index) {
            const std::size_t arg = static_cast<std::size_t>(index);
            if (arg >= args.size() || source.exprs[args[arg]].kind != hir::ExprKind::Lambda)
                continue;

            this->body->emit(Op::BIND_THIS, 0, base + index, base + index, receiverReg);
        }
    }
}
