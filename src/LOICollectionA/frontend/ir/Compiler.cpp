#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "LOICollectionA/frontend/ir/Mir.h"
#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/ir/Compiler.h"

namespace LOICollection::frontend::ir {

    namespace {
        std::string qualifiedName(const VariableNode& node) {
            return node.isStaticField ? node.staticClassName + "::" + node.name : node.name;
        }

        std::string qualifiedName(const MemberAccessNode& node) {
            return node.isStaticAccess
                ? node.staticClassName + "::" + node.memberName
                : node.memberName;
        }
    }

    Compiler::Compiler(DiagnosticEngine& diag) : current(std::ref(chunk)), diagnostics(diag) {}

    MirChunk Compiler::compile(ASTNode& root) {
        // Register 0 holds the value HALT returns; user slots start at 1 so
        // that the final MOVE can never clobber a variable.
        this->pushScope(false, 1);

        int lastValueReg = -1;

        if (root.getType() == ASTNode::Type::Program) {
            auto& program = static_cast<ProgramNode&>(root);

            for (auto& part : program.parts) {
                if (part->getType() == ASTNode::Type::Class) {
                    auto& cls = static_cast<ClassNode&>(*part);
                    this->classNodes.insert_or_assign(cls.name, std::ref(cls));
                }
            }

            for (auto& part : program.parts) {
                switch (part->getType()) {
                    case ASTNode::Type::Class:
                        this->registerClassMeta(static_cast<ClassNode&>(*part));
                        break;
                    case ASTNode::Type::FunctionDef:
                        this->registerFunctionMeta(static_cast<FunctionDefNode&>(*part));
                        break;
                    default:
                        break;
                }
            }

            for (auto node : this->bodyOrder) {
                ASTNode& target = node.get();
                switch (target.getType()) {
                    case ASTNode::Type::Class:
                        this->compileClassBodies(static_cast<ClassNode&>(target));
                        break;
                    case ASTNode::Type::FunctionDef:
                        this->compileFunctionBody(static_cast<FunctionDefNode&>(target));
                        break;
                    default:
                        break;
                }
            }

            for (auto& part : program.parts) {
                auto type = part->getType();
                if (type == ASTNode::Type::Class ||
                    type == ASTNode::Type::FunctionDef ||
                    type == ASTNode::Type::Using ||
                    type == ASTNode::Type::Trait ||
                    type == ASTNode::Type::Impl) {
                    continue;
                }

                int reg = this->compilePart(*part);
                if (reg >= 0)
                    lastValueReg = reg;
            }
        } else {
            lastValueReg = this->compilePart(root);
        }

        if (lastValueReg >= 0) {
            this->current.get().emit(MirOp::MOVE, 0, 0, lastValueReg, -1);
        } else {
            this->current.get().emit(
                MirOp::LOAD_CONST, this->addConstant(std::string("")), 0, -1, -1);
        }

        this->current.get().emit(MirOp::HALT);

        this->chunk.slotCount = this->closeScope();

        return std::move(chunk);
    }

    int Compiler::compilePart(ASTNode& node) {
        node.accept(*this);
        int reg = this->lastResultReg;
        this->lastResultReg = -1;
        return reg;
    }

    int Compiler::compileValue(ExprNode& node, const SourceLocation& loc) {
        const int hint = std::exchange(this->dstHint, -1);

        node.accept(*this);
        int reg = this->lastResultReg;
        this->lastResultReg = -1;

        if (reg >= 0 && node.type.kind == TypeKind::Optional && !node.preserveOptional) {
            const int dst = this->allocReg();
            this->current.get().emit(MirOp::UNWRAP, 0, dst, reg, -1, loc, node.type);
            reg = dst;
        }

        return this->finishHint(hint, reg, loc);
    }

    int Compiler::compileNone(const SourceLocation& loc) {
        const int dst = this->takeDst();
        const int idx = this->addConstant(std::string(""));
        this->current.get().emit(MirOp::LOAD_CONST, idx, dst, -1, -1, loc);
        return dst;
    }

    int Compiler::compileInto(int hint, ASTNode& node, const SourceLocation& loc) {
        if (hint < 0)
            return this->compilePart(node);

        if (auto leaf = this->tryEmitLeaf(node, hint, loc))
            return *leaf;

        this->dstHint = hint;
        node.accept(*this);
        int reg = this->lastResultReg;
        this->lastResultReg = -1;

        return this->finishHint(hint, reg, loc);
    }

    int Compiler::compileArg(int hint, ExprNode& node, const SourceLocation& loc) {
        if (hint >= 0) {
            if (auto leaf = this->tryEmitLeaf(node, hint, loc))
                return *leaf;

            this->dstHint = hint;
        }

        return this->compileValue(node, loc);
    }

    int Compiler::finishHint(int hint, int reg, const SourceLocation& loc) {
        if (reg < 0) {
            reg = hint >= 0 ? hint : this->allocReg();
            this->current.get().emit(
                MirOp::LOAD_CONST, this->addConstant(std::string("")), reg, -1, -1, loc);
            return reg;
        }

        if (hint >= 0 && reg != hint) {
            this->current.get().emit(MirOp::MOVE, 0, hint, reg, -1, loc);
            return hint;
        }

        return reg;
    }

    std::optional<int> Compiler::tryEmitLeaf(ASTNode& node, int dst, const SourceLocation& loc) {
        switch (node.getType()) {
            case ASTNode::Type::Value: {
                auto& value = static_cast<ValueNode&>(node);
                const int idx = this->addConstant(value.value);

                switch (value.value.index()) {
                    case 0: case 1: case 2: case 3: case 7:
                        break;
                    default:
                        this->diagnostics.addError(loc, "Unsupported constant value type");
                        return std::nullopt;
                }

                this->current.get().emit(MirOp::LOAD_CONST, idx, dst, -1, -1, loc);
                return dst;
            }
            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(node);
                if (auto slot = this->resolveSlot(qualifiedName(var))) {
                    this->current.get().emit(MirOp::LOAD_SLOT, *slot, dst, -1, -1, loc);
                    return dst;
                }

                this->current.get().emit(
                    MirOp::LOAD_VAR, this->addConstant(qualifiedName(var)), dst, -1, -1, loc);
                return dst;
            }
            case ASTNode::Type::This:
                this->current.get().emit(MirOp::LOAD_THIS, 0, dst, -1, -1, loc);
                return dst;
            default:
                return std::nullopt;
        }
    }

    int Compiler::emitBinary(
        MirOp genericOp, MirOp intOp, bool isInt,
        int lhs, int rhs, const SourceLocation& loc, const TypeInfo& type
    ) {
        const int dst = this->allocReg();
        this->current.get().emit(isInt ? intOp : genericOp, 0, dst, lhs, rhs, loc, type);
        return dst;
    }

    int Compiler::emitBoolConst(bool value, const SourceLocation& loc) {
        const int dst = this->takeDst();
        this->current.get().emit(MirOp::LOAD_CONST, this->addConstant(value), dst, -1, -1, loc);
        return dst;
    }

    void Compiler::pushScope(bool hasThis, int base, bool inherits) {
        Scope scope;
        scope.base = base;
        scope.next = base;
        scope.hasThis = hasThis || (!this->scopes.empty() && this->scopes.back().hasThis);
        scope.inherits = inherits;
        this->scopes.push_back(std::move(scope));
    }

    int Compiler::closeScope() {
        const int slotCount = this->scopes.back().next;
        this->scopes.pop_back();
        return slotCount;
    }

    int Compiler::declareSlot(const std::string& name) {
        Scope& scope = this->scopes.back();
        auto [it, inserted] = scope.slots.emplace(name, scope.next);
        if (inserted)
            ++scope.next;

        return it->second;
    }

    void Compiler::predeclareLocals(ASTNode& body) {
        auto* seq = dynamic_cast<SequenceNode*>(&body);
        if (!seq)
            return;

        for (auto& part : seq->parts) {
            if (!part || part->getType() != ASTNode::Type::Assignment)
                continue;

            auto& assign = static_cast<AssignmentNode&>(*part);
            if (!assign.isDeclaration || !assign.target)
                continue;

            if (assign.target->getType() != ASTNode::Type::Variable)
                continue;

            auto& var = static_cast<VariableNode&>(*assign.target);
            if (var.isStaticField)
                continue;

            this->declareSlot(qualifiedName(var));
        }
    }

    std::optional<int> Compiler::resolveSlot(const std::string& name) const {
        for (auto scope = this->scopes.rbegin(); scope != this->scopes.rend(); ++scope) {
            if (auto it = scope->slots.find(name); it != scope->slots.end())
                return it->second;

            if (!scope->inherits)
                break;
        }

        return std::nullopt;
    }

    int Compiler::emitLoad(const std::string& name, const SourceLocation& loc) {
        if (auto slot = this->resolveSlot(name))
            return *slot;

        const int dst = this->takeDst();
        this->current.get().emit(MirOp::LOAD_VAR, this->addConstant(name), dst, -1, -1, loc);
        return dst;
    }

    int Compiler::emitLoadInto(int dst, const std::string& name, const SourceLocation& loc) {
        if (auto slot = this->resolveSlot(name)) {
            this->current.get().emit(MirOp::LOAD_SLOT, *slot, dst, -1, -1, loc);
            return dst;
        }

        this->current.get().emit(MirOp::LOAD_VAR, this->addConstant(name), dst, -1, -1, loc);
        return dst;
    }

    void Compiler::emitStore(const std::string& name, int srcReg, const SourceLocation& loc) {
        if (auto slot = this->resolveSlot(name)) {
            this->current.get().emit(MirOp::STORE_SLOT, *slot, -1, srcReg, -1, loc);
            return;
        }

        this->current.get().emit(MirOp::STORE_VAR, this->addConstant(name), -1, srcReg, -1, loc);
    }

    int Compiler::fieldSlotOf(const TypeInfo& owner, const std::string& memberName) const {
        if (owner.kind != TypeKind::Object)
            return -1;

        auto classIt = this->classIndices.find(owner.className);
        if (classIt != this->classIndices.end()) {
            const auto& names = this->chunk.classes[static_cast<size_t>(classIt->second)].fieldNames;

            auto it = std::ranges::find(names, memberName);
            return it == names.end() ? -1 : static_cast<int>(std::distance(names.begin(), it));
        }

        const auto fields = ClassCall::getInstance().getFields(owner.className);

        auto it = std::ranges::find(fields, memberName);
        return it == fields.end() ? -1 : static_cast<int>(std::distance(fields.begin(), it));
    }

    int Compiler::emitLoadField(const MemberAccessNode& node) {
        const int objReg = this->compileValue(*node.target, node.loc);
        const int dst = this->allocReg();
        this->emitLoadFieldInto(dst, objReg, node);
        return dst;
    }

    void Compiler::emitLoadFieldInto(int dst, int objReg, const MemberAccessNode& node) {
        const int slot = this->fieldSlotOf(node.target->type, node.memberName);
        if (slot >= 0) {
            this->current.get().emit(MirOp::LOAD_FIELD_SLOT, slot, dst, objReg, -1, node.loc);
            return;
        }

        this->current.get().emit(
            MirOp::LOAD_FIELD, this->addConstant(node.memberName), dst, objReg, -1, node.loc);
    }

    void Compiler::emitStoreField(int objReg, int valReg, const MemberAccessNode& node) {
        const int slot = this->fieldSlotOf(node.target->type, node.memberName);
        if (slot >= 0) {
            this->current.get().emit(MirOp::STORE_FIELD_SLOT, slot, -1, objReg, valReg, node.loc);
            return;
        }

        this->current.get().emit(
            MirOp::STORE_FIELD, this->addConstant(node.memberName), -1, objReg, valReg, node.loc);
    }

    ClassLookup Compiler::classLookup() const {
        return [this](const std::string& name) -> ClassNode* {
            auto it = this->classNodes.find(name);

            return it == this->classNodes.end() ? nullptr : &it->second.get();
        };
    }

    std::optional<ValueNode::ValueType> Compiler::constantValue(ExprNode& node) const {
        if (node.getType() == ASTNode::Type::Value)
            return static_cast<ValueNode&>(node).value;

        if (node.getType() == ASTNode::Type::Array) {
            auto& array = static_cast<ArrayNode&>(node);
            auto result = std::make_shared<ArrayValue>();
            result->elements.reserve(array.elements.size());

            for (auto& element : array.elements) {
                auto value = this->constantValue(*element);
                if (!value.has_value())
                    return std::nullopt;

                result->elements.push_back(*value);
            }

            return result;
        }

        return std::nullopt;
    }

    int Compiler::addNativeCall(const std::string& className, const std::string& name, int argCount, bool isStatic) {
        for (size_t i = 0; i < this->chunk.nativeCalls.size(); ++i) {
            const auto& meta = this->chunk.nativeCalls[i];
            if (meta.className == className && meta.name == name && meta.argCount == argCount && meta.isStatic == isStatic)
                return static_cast<int>(i);
        }

        this->chunk.nativeCalls.push_back({ className, name, argCount, isStatic });
        return static_cast<int>(this->chunk.nativeCalls.size() - 1);
    }

    int Compiler::addConstant(const ValueNode::ValueType& val) {
        this->current.get().constants.push_back(val);
        return static_cast<int>(this->current.get().constants.size() - 1);
    }

    int Compiler::addFunction(const std::string& name, int argCount) {
        this->current.get().functions.push_back({name, argCount});
        return static_cast<int>(this->current.get().functions.size() - 1);
    }

    int Compiler::addMacro(const std::string& name, int argCount) {
        this->current.get().macros.push_back({name, argCount});
        return static_cast<int>(this->current.get().macros.size() - 1);
    }

    int Compiler::addLambda(int bodyIndex, int argCount, int captureCount) {
        this->current.get().lambdas.push_back({bodyIndex, argCount, captureCount});
        return static_cast<int>(this->current.get().lambdas.size() - 1);
    }

    int Compiler::addVirtualCall(int classIndex, int ordinal, int argCount) {
        this->current.get().virtualCalls.push_back({classIndex, ordinal, argCount});
        return static_cast<int>(this->current.get().virtualCalls.size() - 1);
    }

    int Compiler::addByNameCall(const std::string& methodName, int argCount) {
        this->current.get().byNameCalls.push_back({methodName, argCount});
        return static_cast<int>(this->current.get().byNameCalls.size() - 1);
    }

    int Compiler::addSuperCall(int constructorIndex, int argCount) {
        this->current.get().superCalls.push_back({constructorIndex, argCount});
        return static_cast<int>(this->current.get().superCalls.size() - 1);
    }

    void Compiler::visit(ValueNode& node) {
        const int idx = this->addConstant(node.value);

        switch (node.value.index()) {
            case 0: case 1: case 2: case 3: case 7:
                break;
            default:
                this->diagnostics.addError(node.loc, "Unsupported constant value type");
                return;
        }

        this->lastResultReg = this->tryEmitLeaf(node, this->takeDst(), node.loc).value_or(-1);
    }

    void Compiler::visit(VariableNode& node) {
        int reg = this->emitLoad(qualifiedName(node), node.loc);

        if (node.type.kind == TypeKind::Optional && !node.preserveOptional) {
            const int dst = this->allocReg();
            this->current.get().emit(MirOp::UNWRAP, 0, dst, reg, -1, node.loc, node.type);
            reg = dst;
        }

        this->lastResultReg = reg;
    }

    void Compiler::visit(AssignmentNode& node) {
        int valReg = node.value
            ? this->compileValue(*node.value, node.loc)
            : this->compileNone(node.loc);

        switch (node.target->getType()) {
            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(*node.target);
                this->emitStore(qualifiedName(var), valReg, node.loc);
                break;
            }
            case ASTNode::Type::MemberAccess: {
                auto& member = static_cast<MemberAccessNode&>(*node.target);
                if (member.isStaticAccess) {
                    this->emitStore(qualifiedName(member), valReg, node.loc);
                    break;
                }

                const int objReg = this->compileValue(*member.target, node.loc);
                this->emitStoreField(objReg, valReg, member);
                break;
            }
            case ASTNode::Type::Index: {
                auto& indexNode = static_cast<IndexAccessNode&>(*node.target);

                const int objReg = this->finishHint(-1, this->compilePart(*indexNode.target), node.loc);
                const int idxReg = this->finishHint(-1, this->compilePart(*indexNode.index), node.loc);

                this->current.get().emit(MirOp::STORE_INDEX, 0, -1, objReg, idxReg, valReg, node.loc);
                break;
            }
            default:
                this->diagnostics.addError(node.loc, "Invalid assignment target");
                break;
        }

        this->lastResultReg = valReg;
    }

    void Compiler::visit(CompoundAssignNode& node) {
        switch (node.target->getType()) {
            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(*node.target);
                const std::string name = qualifiedName(var);

                int lhs = this->emitLoad(name, node.loc);
                if (var.type.kind == TypeKind::Optional && !var.preserveOptional) {
                    const int unwrapped = this->allocReg();
                    this->current.get().emit(MirOp::UNWRAP, 0, unwrapped, lhs, -1, node.loc, var.type);
                    lhs = unwrapped;
                }

                const int rhs = this->compileValue(*node.value, node.loc);
                this->lastResultReg = this->emitArithmeticOp(node.op, var.type, node.value->type, lhs, rhs, node.loc);
                this->emitStore(name, this->lastResultReg, node.loc);
                break;
            }
            case ASTNode::Type::MemberAccess: {
                auto& member = static_cast<MemberAccessNode&>(*node.target);
                if (member.isStaticAccess) {
                    const std::string name = qualifiedName(member);

                    int lhs = this->emitLoad(name, node.loc);
                    if (member.type.kind == TypeKind::Optional && !member.preserveOptional) {
                        const int unwrapped = this->allocReg();
                        this->current.get().emit(MirOp::UNWRAP, 0, unwrapped, lhs, -1, node.loc, member.type);
                        lhs = unwrapped;
                    }

                    const int rhs = this->compileValue(*node.value, node.loc);
                    this->lastResultReg = this->emitArithmeticOp(node.op, member.type, node.value->type, lhs, rhs, node.loc);
                    this->emitStore(name, this->lastResultReg, node.loc);
                    break;
                }

                const int objReg = this->compileValue(*member.target, node.loc);

                int lhs = this->allocReg();
                this->emitLoadFieldInto(lhs, objReg, member);
                if (member.type.kind == TypeKind::Optional && !member.preserveOptional) {
                    const int unwrapped = this->allocReg();
                    this->current.get().emit(MirOp::UNWRAP, 0, unwrapped, lhs, -1, node.loc, member.type);
                    lhs = unwrapped;
                }

                const int rhs = this->compileValue(*node.value, node.loc);
                this->lastResultReg = this->emitArithmeticOp(node.op, member.type, node.value->type, lhs, rhs, node.loc);
                this->emitStoreField(objReg, this->lastResultReg, member);
                break;
            }
            case ASTNode::Type::Index: {
                auto& indexNode = static_cast<IndexAccessNode&>(*node.target);

                const int objReg = this->compileValue(*indexNode.target, node.loc);
                const int idxReg = this->compileValue(*indexNode.index, node.loc);

                int lhs = this->allocReg();
                this->current.get().emit(MirOp::LOAD_INDEX, 0, lhs, objReg, idxReg, node.loc);
                if (indexNode.type.kind == TypeKind::Optional && !indexNode.preserveOptional) {
                    const int unwrapped = this->allocReg();
                    this->current.get().emit(MirOp::UNWRAP, 0, unwrapped, lhs, -1, node.loc, indexNode.type);
                    lhs = unwrapped;
                }

                const int rhs = this->compileValue(*node.value, node.loc);
                this->lastResultReg = this->emitArithmeticOp(node.op, indexNode.type, node.value->type, lhs, rhs, node.loc);
                this->current.get().emit(
                    MirOp::STORE_INDEX, 0, -1, objReg, idxReg, this->lastResultReg, node.loc);
                break;
            }
            default:
                this->diagnostics.addError(node.loc, "Invalid compound assignment target");
                break;
        }
    }

    void Compiler::visit(CoalesceNode& node) {
        const int dst = this->allocReg();
        this->compileArg(dst, *node.left, node.loc);

        const int cond = this->allocReg();
        this->current.get().emit(MirOp::IS_NONE, 0, cond, dst, -1, node.loc);

        const size_t jmpSkipIdx = this->current.get().emit(
            MirOp::JMP_IF_FALSE, 0, -1, cond, -1, node.loc);

        this->compileArg(dst, *node.right, node.loc);

        const size_t jmpEndIdx = this->current.get().emit(MirOp::JMP, 0, node.loc);

        const int endPos = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpSkipIdx, endPos - static_cast<int>(jmpSkipIdx) - 1);
        this->current.get().patchJump(jmpEndIdx, endPos - static_cast<int>(jmpEndIdx) - 1);

        this->lastResultReg = dst;
    }

    void Compiler::visit(RangeNode& node) {
        this->diagnostics.addError(node.loc, "Range expression is only allowed as a for-in iterable");
    }

    void Compiler::visit(ForInNode& node) {
        size_t uid = this->forInCounter++;

        auto protocol = iterableProtocol(node.iterable->getType(), node.iterable->type, this->classLookup());
        if (!protocol) {
            this->diagnostics.addError(node.loc,
                "for-in iterable does not provide an iteration protocol");

            return;
        }

        if (protocol->shape == IterableShape::Counter)
            this->compileForInCounter(node, uid);
        else
            this->compileForInIterable(node, uid, *protocol);
    }

    int Compiler::emitArithmeticOp(
        const std::string& op, const TypeInfo& leftType, const TypeInfo& rightType,
        int lhs, int rhs, const SourceLocation& loc
    ) {
        const bool isInt = leftType.kind == TypeKind::Int && rightType.kind == TypeKind::Int;
        const TypeInfo resultType = isInt ? leftType : TypeInfo{};

        if (op == "+") return this->emitBinary(MirOp::ADD, MirOp::ADD_I, isInt, lhs, rhs, loc, resultType);
        if (op == "-") return this->emitBinary(MirOp::SUB, MirOp::SUB_I, isInt, lhs, rhs, loc, resultType);
        if (op == "*") return this->emitBinary(MirOp::MUL, MirOp::MUL_I, isInt, lhs, rhs, loc, resultType);
        if (op == "/") return this->emitBinary(MirOp::DIV, MirOp::DIV, false, lhs, rhs, loc);
        if (op == "%") return this->emitBinary(MirOp::MOD, MirOp::MOD_I, isInt, lhs, rhs, loc, resultType);

        this->diagnostics.addError(loc, "Unknown compound assignment op: " + op);
        return lhs;
    }

    void Compiler::visit(IfNode& node) {
        const int cond = this->compileValue(*node.condition, node.loc);

        const size_t jmpFalseIdx = this->current.get().emit(
            MirOp::JMP_IF_FALSE, 0, -1, cond, -1, node.loc);

        const int dst = this->allocReg();
        this->compileInto(dst, *node.trueBranch, node.loc);

        const size_t jmpEndIdx = this->current.get().emit(MirOp::JMP, 0, node.loc);

        const int falseStart = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpFalseIdx, falseStart - static_cast<int>(jmpFalseIdx) - 1);

        if (node.falseBranch)
            this->compileInto(dst, *node.falseBranch, node.loc);
        else
            this->finishHint(dst, -1, node.loc);

        const int endPos = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpEndIdx, endPos - static_cast<int>(jmpEndIdx) - 1);

        this->lastResultReg = dst;
    }

    void Compiler::visit(WhileNode& node) {
        const size_t loopStart = this->current.get().currentIP();

        const int cond = this->compileValue(*node.condition, node.loc);

        const size_t jmpFalseIdx = this->current.get().emit(
            MirOp::JMP_IF_FALSE, 0, -1, cond, -1, node.loc);

        this->loopStack.push_back(LoopContext{});
        this->loopStack.back().continueTarget = loopStart;

        node.body->accept(*this);

        const size_t jmpBackIdx = this->current.get().emit(MirOp::JMP, 0, node.loc);
        this->current.get().patchJump(jmpBackIdx, static_cast<int>(loopStart) - static_cast<int>(jmpBackIdx) - 1);

        const size_t exitPos = this->current.get().currentIP();
        this->current.get().patchJump(jmpFalseIdx, static_cast<int>(exitPos) - static_cast<int>(jmpFalseIdx) - 1);

        for (size_t idx : this->loopStack.back().breakJumps)
            this->current.get().patchJump(idx, static_cast<int>(exitPos) - static_cast<int>(idx) - 1);
        for (size_t idx : this->loopStack.back().continueJumps)
            this->current.get().patchJump(idx, static_cast<int>(loopStart) - static_cast<int>(idx) - 1);

        this->loopStack.pop_back();

        this->lastResultReg = this->finishHint(-1, -1, node.loc);
    }

    void Compiler::visit(ForNode& node) {
        if (node.init)
            this->compileValue(*node.init, node.loc);

        const size_t loopStart = this->current.get().currentIP();

        size_t jmpFalseIdx = static_cast<size_t>(-1);
        if (node.condition) {
            const int cond = this->compileValue(*node.condition, node.loc);
            jmpFalseIdx = this->current.get().emit(MirOp::JMP_IF_FALSE, 0, -1, cond, -1, node.loc);
        }

        this->loopStack.push_back(LoopContext{});

        node.body->accept(*this);

        const size_t continuePos = this->current.get().currentIP();
        this->loopStack.back().continueTarget = continuePos;

        if (node.step)
            this->compileValue(*node.step, node.loc);

        const size_t jmpBackIdx = this->current.get().emit(MirOp::JMP, 0, node.loc);
        this->current.get().patchJump(jmpBackIdx, static_cast<int>(loopStart) - static_cast<int>(jmpBackIdx) - 1);

        const size_t exitPos = this->current.get().currentIP();
        if (jmpFalseIdx != static_cast<size_t>(-1))
            this->current.get().patchJump(jmpFalseIdx, static_cast<int>(exitPos) - static_cast<int>(jmpFalseIdx) - 1);

        for (size_t idx : this->loopStack.back().breakJumps)
            this->current.get().patchJump(idx, static_cast<int>(exitPos) - static_cast<int>(idx) - 1);
        for (size_t idx : this->loopStack.back().continueJumps)
            this->current.get().patchJump(idx, static_cast<int>(continuePos) - static_cast<int>(idx) - 1);

        this->loopStack.pop_back();

        this->lastResultReg = this->finishHint(-1, -1, node.loc);
    }

    void Compiler::visit(BreakNode& node) {
        if (this->loopStack.empty()) {
            this->diagnostics.addError(node.loc, "'break' can only be used inside a loop");
            return;
        }

        size_t idx = this->current.get().emit(MirOp::JMP, 0, node.loc);
        this->loopStack.back().breakJumps.push_back(idx);
    }

    void Compiler::visit(ContinueNode& node) {
        if (this->loopStack.empty()) {
            this->diagnostics.addError(node.loc, "'continue' can only be used inside a loop");
            return;
        }

        size_t idx = this->current.get().emit(MirOp::JMP, 0, node.loc);
        this->loopStack.back().continueJumps.push_back(idx);
    }

    void Compiler::visit(CompareNode& node) {
        const int lhs = this->compileValue(*node.left, node.loc);
        const int rhs = this->compileValue(*node.right, node.loc);

        const bool isInt = node.left->type.kind == TypeKind::Int && node.right->type.kind == TypeKind::Int;
        const TypeInfo resultType = isInt ? node.left->type : TypeInfo{};

        if (node.op == "==") this->lastResultReg = this->emitBinary(MirOp::CMP_EQ, MirOp::CMP_EQ_I, isInt, lhs, rhs, node.loc, resultType);
        else if (node.op == "!=") this->lastResultReg = this->emitBinary(MirOp::CMP_NE, MirOp::CMP_NE_I, isInt, lhs, rhs, node.loc, resultType);
        else if (node.op == ">") this->lastResultReg = this->emitBinary(MirOp::CMP_GT, MirOp::CMP_GT_I, isInt, lhs, rhs, node.loc, resultType);
        else if (node.op == "<") this->lastResultReg = this->emitBinary(MirOp::CMP_LT, MirOp::CMP_LT_I, isInt, lhs, rhs, node.loc, resultType);
        else if (node.op == ">=") this->lastResultReg = this->emitBinary(MirOp::CMP_GE, MirOp::CMP_GE_I, isInt, lhs, rhs, node.loc, resultType);
        else if (node.op == "<=") this->lastResultReg = this->emitBinary(MirOp::CMP_LE, MirOp::CMP_LE_I, isInt, lhs, rhs, node.loc, resultType);
        else this->diagnostics.addError(node.loc, "Unknown compare op: " + node.op);
    }

    void Compiler::visit(LogicalNode& node) {
        const bool isAnd = (node.op == "&&");

        if (node.left->getType() == ASTNode::Type::Value) {
            auto& leftVal = static_cast<const ValueNode&>(*node.left).value;
            if (std::holds_alternative<bool>(leftVal)) {
                bool leftBool = std::get<bool>(leftVal);

                if (isAnd) {
                    if (leftBool) {
                        const int lhs = this->emitBoolConst(true, node.loc);
                        const int rhs = this->compileValue(*node.right, node.loc);
                        this->lastResultReg = this->emitBinary(
                            MirOp::LOGIC_AND, MirOp::LOGIC_AND, false, lhs, rhs, node.loc);
                    } else {
                        this->lastResultReg = this->emitBoolConst(false, node.loc);
                    }
                } else {
                    if (leftBool) {
                        this->lastResultReg = this->emitBoolConst(true, node.loc);
                    } else {
                        const int lhs = this->emitBoolConst(false, node.loc);
                        const int rhs = this->compileValue(*node.right, node.loc);
                        this->lastResultReg = this->emitBinary(
                            MirOp::LOGIC_OR, MirOp::LOGIC_OR, false, lhs, rhs, node.loc);
                    }
                }
                return;
            }
        }

        const int dst = this->allocReg();
        const int lhs = this->compileValue(*node.left, node.loc);

        const size_t jumpToShort = this->current.get().emit(
            isAnd ? MirOp::JMP_IF_FALSE : MirOp::JMP_IF_TRUE, 0, -1, lhs, -1, node.loc);

        const int rhs = this->compileValue(*node.right, node.loc);
        this->current.get().emit(isAnd ? MirOp::LOGIC_AND : MirOp::LOGIC_OR, 0, dst, lhs, rhs, node.loc);

        const size_t jumpToEnd = this->current.get().emit(MirOp::JMP, 0, node.loc);

        const int shortStart = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jumpToShort, shortStart - static_cast<int>(jumpToShort) - 1);

        this->current.get().emit(
            MirOp::LOAD_CONST, this->addConstant(isAnd ? false : true), dst, -1, -1, node.loc);

        const int endPos = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jumpToEnd, endPos - static_cast<int>(jumpToEnd) - 1);

        this->lastResultReg = dst;
    }

    void Compiler::visit(FunctionNode& node) {
        const int argCount = static_cast<int>(node.args.size());
        const int base = this->reserveRegs(argCount);

        for (int i = 0; i < argCount; ++i)
            this->compileArg(base + i, *node.args[i], node.loc);

        const int metaIdx = this->addFunction(node.namespaces + "::" + node.name, argCount);
        const int dst = this->allocReg();

        this->current.get().emitCall(MirOp::CALL, metaIdx, dst, base, -1, argCount, node.loc);
        this->lastResultReg = dst;
    }

    void Compiler::visit(MacroNode& node) {
        const int argCount = static_cast<int>(node.args.size());
        const int base = this->reserveRegs(argCount);

        for (int i = 0; i < argCount; ++i)
            this->compileArg(base + i, *node.args[i], node.loc);

        const int metaIdx = this->addMacro(node.name, argCount);
        const int dst = this->allocReg();

        this->current.get().emitCall(MirOp::CALL_MACRO, metaIdx, dst, base, -1, argCount, node.loc);
        this->lastResultReg = dst;
    }

    void Compiler::visit(ArithmeticNode& node) {
        const int lhs = this->compileValue(*node.left, node.loc);
        const int rhs = this->compileValue(*node.right, node.loc);

        const bool isInt = node.type.kind == TypeKind::Int;

        if (node.op == "+") this->lastResultReg = this->emitBinary(MirOp::ADD, MirOp::ADD_I, isInt, lhs, rhs, node.loc, node.type);
        else if (node.op == "-") this->lastResultReg = this->emitBinary(MirOp::SUB, MirOp::SUB_I, isInt, lhs, rhs, node.loc, node.type);
        else if (node.op == "*") this->lastResultReg = this->emitBinary(MirOp::MUL, MirOp::MUL_I, isInt, lhs, rhs, node.loc, node.type);
        else if (node.op == "/") this->lastResultReg = this->emitBinary(MirOp::DIV, MirOp::DIV, false, lhs, rhs, node.loc);
        else if (node.op == "%") this->lastResultReg = this->emitBinary(MirOp::MOD, MirOp::MOD_I, isInt, lhs, rhs, node.loc, node.type);
        else if (node.op == "^") this->lastResultReg = this->emitBinary(MirOp::POW, MirOp::POW, false, lhs, rhs, node.loc);
        else this->diagnostics.addError(node.loc, "Unknown arithmetic op: " + node.op);
    }

    void Compiler::visit(UnaryNode& node) {
        const int src = this->compileValue(*node.operand, node.loc);

        if (node.op == "-") {
            const bool isInt = node.type.kind == TypeKind::Int;
            const int dst = this->allocReg();
            this->current.get().emit(
                isInt ? MirOp::NEG_I : MirOp::NEG, 0, dst, src, -1, node.loc, node.type);
            this->lastResultReg = dst;
        } else if (node.op == "!") {
            const int dst = this->allocReg();
            this->current.get().emit(MirOp::NOT, 0, dst, src, -1, node.loc);
            this->lastResultReg = dst;
        } else if (node.op == "+") {
            this->lastResultReg = src;
        } else {
            this->diagnostics.addError(node.loc, "Unknown unary op: " + node.op);
        }
    }

    void Compiler::compileSequence(SequenceNode& node) {
        const int hint = std::exchange(this->dstHint, -1);

        if (node.parts.empty()) {
            this->lastResultReg = this->finishHint(hint, -1, {});
            return;
        }

        for (size_t i = 0; i < node.parts.size(); ++i) {
            this->lastResultReg = -1;

            if (hint >= 0 && i + 1 == node.parts.size())
                this->dstHint = hint;

            node.parts[i]->accept(*this);
        }

        this->lastResultReg = this->finishHint(hint, this->lastResultReg, {});
    }

    void Compiler::visit(UsingNode&) {}

    void Compiler::visit(ImportNode&) {}

    void Compiler::visit(ComponentNode&) {}

    void Compiler::visit(ProgramNode& node) {
        compileSequence(node);
    }

    void Compiler::visit(BlockNode& node) {
        compileSequence(node);
    }

    void Compiler::visit(ClassNode& node) {
        this->registerClassMeta(node);
        this->compileClassBodies(node);
    }

    void Compiler::visit(ReturnNode& node) {
        const int reg = node.value ? this->compilePart(*node.value) : -1;

        this->current.get().emit(MirOp::RETURN, 0, -1, reg, -1, node.loc);
        this->lastResultReg = -1;
    }

    void Compiler::visit(NewNode& node) {
        const int argCount = static_cast<int>(node.args.size());
        const int base = this->reserveRegs(argCount);

        for (int i = 0; i < argCount; ++i)
            this->compileArg(base + i, *node.args[i], node.loc);

        int dst = -1;
        auto it = this->classIndices.find(node.className);
        if (it == this->classIndices.end()) {
            if (!ClassCall::getInstance().isRegistered(node.className)) {
                this->diagnostics.addError(node.loc, "Unknown class: " + node.className);
                return;
            }

            const int metaIdx = this->addNativeCall(node.className, "", argCount);
            dst = this->allocReg();
            this->current.get().emitCall(MirOp::NEW_NATIVE, metaIdx, dst, base, -1, argCount, node.loc);
        } else {
            dst = this->allocReg();
            this->current.get().emitCall(MirOp::NEW, it->second, dst, base, -1, argCount, node.loc);
        }

        this->lastResultReg = dst;

        if (node.declarativeBlock)
            this->compileDeclarativeBlock(*node.declarativeBlock, node.receiverName);
    }

    void Compiler::compileDeclarativeBlock(BlockNode& block, const std::string& receiverName) {
        std::string receiver = receiverName;
        int receiverSlot = -1;

        if (receiver.empty()) {
            receiver = ".form" + std::to_string(this->declarativeCounter++);
            receiverSlot = this->declareSlot(receiver);
        }

        const int objReg = this->finishHint(-1, this->lastResultReg, {});
        this->lastResultReg = -1;

        if (receiverSlot >= 0)
            this->current.get().emit(MirOp::STORE_SLOT, receiverSlot, -1, objReg, -1);
        else
            this->emitStore(receiver, objReg, {});

        for (auto& part : block.parts)
            this->desugarDeclarativeStatements(part, receiver);

        for (auto& part : block.parts) {
            this->lastResultReg = -1;
            part->accept(*this);
        }

        this->lastResultReg = -1;

        if (receiverSlot >= 0) {
            const int dst = this->allocReg();
            this->current.get().emit(MirOp::LOAD_SLOT, receiverSlot, dst, -1, -1);
            this->lastResultReg = dst;
        } else {
            this->lastResultReg = this->emitLoad(receiver, {});
        }
    }

    void Compiler::desugarDeclarativeStatements(std::unique_ptr<ASTNode>& node, const std::string& receiver) {
        switch (node->getType()) {
            case ASTNode::Type::Block: {
                auto& block = static_cast<BlockNode&>(*node);
                for (auto& part : block.parts)
                    this->desugarDeclarativeStatements(part, receiver);
                return;
            }
            case ASTNode::Type::If: {
                auto& ifNode = static_cast<IfNode&>(*node);
                if (ifNode.trueBranch)
                    this->desugarDeclarativeStatements(ifNode.trueBranch, receiver);
                if (ifNode.falseBranch)
                    this->desugarDeclarativeStatements(ifNode.falseBranch, receiver);
                return;
            }
            case ASTNode::Type::While: {
                auto& whileNode = static_cast<WhileNode&>(*node);
                if (whileNode.body)
                    this->desugarDeclarativeStatements(whileNode.body, receiver);
                return;
            }
            case ASTNode::Type::For: {
                auto& forNode = static_cast<ForNode&>(*node);
                if (forNode.body)
                    this->desugarDeclarativeStatements(forNode.body, receiver);
                return;
            }
            case ASTNode::Type::ForIn: {
                auto& forIn = static_cast<ForInNode&>(*node);
                if (forIn.body)
                    this->desugarDeclarativeStatements(forIn.body, receiver);
                return;
            }
            case ASTNode::Type::FuncCall: {
                auto& call = static_cast<FuncCallNode&>(*node);
                if (!call.isFormReceiverCall || call.receiverClassName.empty())
                    return;

                auto methodCall = std::make_unique<MethodCallNode>(
                    call.loc,
                    std::make_unique<VariableNode>(call.loc, receiver),
                    call.name,
                    std::move(call.args)
                );
                methodCall->className = call.receiverClassName;
                methodCall->methodOrdinal = -1;

                node = std::move(methodCall);
                return;
            }
            default:
                return;
        }
    }

    void Compiler::visit(MemberAccessNode& node) {
        if (node.isStaticAccess) {
            int reg = this->emitLoad(qualifiedName(node), node.loc);

            if (node.type.kind == TypeKind::Optional && !node.preserveOptional) {
                const int dst = this->allocReg();
                this->current.get().emit(MirOp::UNWRAP, 0, dst, reg, -1, node.loc, node.type);
                reg = dst;
            }

            this->lastResultReg = reg;
            return;
        }

        if (node.isSafe) {
            const int objReg = this->compileValue(*node.target, node.loc);
            const int dst = this->allocReg();
            const int cond = this->allocReg();

            this->current.get().emit(MirOp::IS_NONE, 0, cond, objReg, -1, node.loc);

            const size_t jmpSkipIdx = this->current.get().emit(
                MirOp::JMP_IF_FALSE, 0, -1, cond, -1, node.loc);

            this->current.get().emit(
                MirOp::LOAD_CONST, this->addConstant(std::monostate{}), dst, -1, -1, node.loc);

            const size_t jmpEndIdx = this->current.get().emit(MirOp::JMP, 0, node.loc);

            const int skipTarget = static_cast<int>(this->current.get().currentIP());
            this->current.get().patchJump(jmpSkipIdx, skipTarget - static_cast<int>(jmpSkipIdx) - 1);

            switch (node.memberKind) {
                case MemberAccessNode::MemberKind::TypeOf:
                    this->current.get().emit(MirOp::TYPE_OF, 0, dst, objReg, -1, node.loc);
                    break;
                case MemberAccessNode::MemberKind::HasValue:
                    this->current.get().emit(MirOp::HAS_VALUE, 0, dst, objReg, -1, node.loc);
                    break;
                case MemberAccessNode::MemberKind::Value:
                    this->current.get().emit(MirOp::UNWRAP, 0, dst, objReg, -1, node.loc);
                    break;
                default:
                    this->emitLoadFieldInto(dst, objReg, node);
                    break;
            }

            const int endPos = static_cast<int>(this->current.get().currentIP());
            this->current.get().patchJump(jmpEndIdx, endPos - static_cast<int>(jmpEndIdx) - 1);

            this->lastResultReg = dst;
            return;
        }

        switch (node.memberKind) {
            case MemberAccessNode::MemberKind::TypeOf: {
                const int objReg = this->finishHint(-1, this->compilePart(*node.target), node.loc);
                const int dst = this->allocReg();
                this->current.get().emit(MirOp::TYPE_OF, 0, dst, objReg, -1, node.loc);
                this->lastResultReg = dst;
                return;
            }
            case MemberAccessNode::MemberKind::Value: {
                const int objReg = this->finishHint(-1, this->compilePart(*node.target), node.loc);
                if (node.target->type.kind != TypeKind::Optional) {
                    this->lastResultReg = objReg;
                    return;
                }

                const int dst = this->allocReg();
                this->current.get().emit(MirOp::UNWRAP, 0, dst, objReg, -1, node.loc);
                this->lastResultReg = dst;
                return;
            }
            case MemberAccessNode::MemberKind::HasValue: {
                const int objReg = this->finishHint(-1, this->compilePart(*node.target), node.loc);
                const int dst = this->allocReg();
                this->current.get().emit(MirOp::HAS_VALUE, 0, dst, objReg, -1, node.loc);
                this->lastResultReg = dst;
                return;
            }
            case MemberAccessNode::MemberKind::Normal:
                break;
        }

        int reg = this->emitLoadField(node);

        if (node.type.kind == TypeKind::Optional && !node.preserveOptional) {
            const int dst = this->allocReg();
            this->current.get().emit(MirOp::UNWRAP, 0, dst, reg, -1, node.loc, node.type);
            reg = dst;
        }

        this->lastResultReg = reg;
    }

    void Compiler::visit(MethodCallNode& node) {
        const bool bindsReceiver = !this->scopes.back().hasThis;
        const int argCount = static_cast<int>(node.args.size());

        if (node.dynamicDispatch) {
            const int base = this->reserveRegs(argCount + 1);
            for (int i = 0; i < argCount; ++i)
                this->compileArg(base + i, *node.args[i], node.loc);

            // Historically BIND_THIS read the value on top of the operand stack,
            // which at this point was the last argument rather than the receiver.
            if (bindsReceiver && argCount > 0) {
                for (int i = 0; i < argCount; ++i) {
                    if (node.args[i]->getType() != ASTNode::Type::Lambda)
                        continue;

                    this->current.get().emit(
                        MirOp::BIND_THIS, 0, base + i, base + i, base + argCount - 1, node.loc);
                }
            }

            this->compileArg(base + argCount, *node.target, node.loc);

            const int metaIdx = this->addByNameCall(node.methodName, argCount);
            const int dst = this->allocReg();

            this->current.get().emitCall(
                MirOp::CALL_METHOD_BY_NAME, metaIdx, dst, base, -1, argCount, node.loc);
            this->lastResultReg = dst;
            return;
        }

        if (node.isStaticCall) {
            const int base = this->reserveRegs(argCount);
            for (int i = 0; i < argCount; ++i)
                this->compileArg(base + i, *node.args[i], node.loc);

            if (ClassCall::getInstance().isRegistered(node.staticClassName)) {
                const int metaIdx = this->addNativeCall(node.staticClassName, node.methodName, argCount, true);
                const int dst = this->allocReg();

                this->current.get().emitCall(
                    MirOp::CALL_NATIVE_METHOD, metaIdx, dst, base, -1, argCount, node.loc);
                this->lastResultReg = dst;
                return;
            }

            auto it = this->classStaticMethodIndices.find(node.staticClassName);
            if (it != this->classStaticMethodIndices.end() && node.methodOrdinal >= 0 &&
                static_cast<size_t>(node.methodOrdinal) < it->second.size()) {
                const int dst = this->allocReg();

                this->current.get().emitCall(
                    MirOp::CALL_FUNC, it->second[node.methodOrdinal], dst, base, -1, argCount, node.loc);
                this->lastResultReg = dst;
            } else {
                this->diagnostics.addError(node.loc, "Unresolved static method call: " + node.methodName);
            }
            return;
        }

        const int base = this->reserveRegs(argCount + 1);
        for (int i = 0; i < argCount; ++i)
            this->compileArg(base + i, *node.args[i], node.loc);

        const int receiver = this->compileArg(base + argCount, *node.target, node.loc);

        if (bindsReceiver) {
            for (int i = 0; i < argCount; ++i) {
                if (node.args[i]->getType() != ASTNode::Type::Lambda)
                    continue;

                this->current.get().emit(MirOp::BIND_THIS, 0, base + i, base + i, receiver, node.loc);
            }
        }

        auto it = classMethodIndices.find(node.className);
        if (it != classMethodIndices.end() && node.methodOrdinal >= 0 &&
            static_cast<size_t>(node.methodOrdinal) < it->second.size()) {
            const int dst = this->allocReg();

            if (node.target->getType() == ASTNode::Type::Super) {
                this->current.get().emitCall(
                    MirOp::CALL_METHOD, it->second[node.methodOrdinal], dst, base, -1, argCount, node.loc);
            } else {
                int classIdx = this->classIndices[node.className];
                int metaIdx = this->addVirtualCall(classIdx, node.methodOrdinal, argCount);

                this->current.get().emitCall(
                    MirOp::CALL_METHOD_VIRTUAL, metaIdx, dst, base, -1, argCount, node.loc);
            }

            this->lastResultReg = dst;
            return;
        }

        if (ClassCall::getInstance().isRegistered(node.className)) {
            const int metaIdx = this->addNativeCall(node.className, node.methodName, argCount);
            const int dst = this->allocReg();

            this->current.get().emitCall(
                MirOp::CALL_NATIVE_METHOD, metaIdx, dst, base, -1, argCount, node.loc);
            this->lastResultReg = dst;
            return;
        }

        this->diagnostics.addError(node.loc, "Unresolved method call: " + node.methodName);
    }

    void Compiler::visit(ThisNode& node) {
        const int dst = this->takeDst();
        this->current.get().emit(MirOp::LOAD_THIS, 0, dst, -1, -1, node.loc);
        this->lastResultReg = dst;
    }

    void Compiler::visit(SuperNode& node) {
        const int dst = this->takeDst();
        this->current.get().emit(MirOp::LOAD_THIS, 0, dst, -1, -1, node.loc);
        this->lastResultReg = dst;
    }

    void Compiler::visit(SuperCallNode& node) {
        const int argCount = static_cast<int>(node.args.size());
        const int base = this->reserveRegs(argCount + 1);

        for (int i = 0; i < argCount; ++i)
            this->compileArg(base + i, *node.args[i], node.loc);

        this->current.get().emit(MirOp::LOAD_THIS, 0, base + argCount, -1, -1, node.loc);

        int constructorIndex = -1;
        if (node.constructorIndex >= 0) {
            auto classIt = this->classIndices.find(node.className);
            if (classIt != this->classIndices.end())
                constructorIndex = this->chunk.classes[classIt->second].constructorIndex;
        }

        const int metaIdx = this->addSuperCall(constructorIndex, argCount);

        this->current.get().emitCall(
            MirOp::CALL_SUPER_CTOR, metaIdx, -1, base, -1, argCount, node.loc);
        this->lastResultReg = -1;
    }

    void Compiler::visit(InstanceOfNode& node) {
        const int objReg = this->compileValue(*node.target, node.loc);
        const int nameIdx = this->addConstant(node.className);
        const int dst = this->allocReg();

        this->current.get().emit(MirOp::INSTANCEOF, nameIdx, dst, objReg, -1, node.loc);
        this->lastResultReg = dst;
    }

    void Compiler::visit(FunctionDefNode& node) {
        this->registerFunctionMeta(node);
        this->compileFunctionBody(node);
    }

    void Compiler::registerFunctionMeta(FunctionDefNode& node) {
        int funcIdx = static_cast<int>(methodCount++);
        this->functionIndices[node.name].push_back(funcIdx);
        this->bodyOrder.emplace_back(std::ref(node));
    }

    void Compiler::compileFunctionBody(FunctionDefNode& node) {
        ir::MethodMeta mm;
        mm.name = node.name;
        mm.classIndex = -1;
        mm.argCount = static_cast<int>(node.decl.params.size());

        int bodyIdx = static_cast<int>(this->chunk.methodBodies.size());
        auto body = std::make_unique<MirChunk>();
        MirChunk& bodyChunk = *body;
        this->chunk.methodBodies.push_back(std::move(body));

        std::reference_wrapper<MirChunk> saved = this->current;
        this->current = std::ref(bodyChunk);
        auto loops = this->suspendLoops();

        this->pushScope(false, 0);
        for (const auto& param : node.decl.params)
            this->declareSlot(param.name);

        int lastReg = -1;
        if (node.decl.body) {
            this->predeclareLocals(*node.decl.body);
            lastReg = this->compilePart(*node.decl.body);
        }

        this->current.get().emit(MirOp::RETURN, 0, -1, lastReg, -1);

        this->current = saved;
        bodyChunk.slotCount = this->closeScope();

        mm.bodyIndex = bodyIdx;

        this->chunk.methods.push_back(std::move(mm));
    }

    void Compiler::visit(FuncCallNode& node) {
        const int argCount = static_cast<int>(node.args.size());

        if (node.isStaticCall) {
            const int base = this->reserveRegs(argCount);
            for (int i = 0; i < argCount; ++i)
                this->compileArg(base + i, *node.args[i], node.loc);

            if (ClassCall::getInstance().isRegistered(node.staticClassName)) {
                const int metaIdx = this->addNativeCall(node.staticClassName, node.name, argCount, true);
                const int dst = this->allocReg();

                this->current.get().emitCall(
                    MirOp::CALL_NATIVE_METHOD, metaIdx, dst, base, -1, argCount, node.loc);
                this->lastResultReg = dst;
                return;
            }

            auto it = this->classStaticMethodIndices.find(node.staticClassName);
            if (it != this->classStaticMethodIndices.end() && node.methodOrdinal >= 0 &&
                static_cast<size_t>(node.methodOrdinal) < it->second.size()) {
                const int dst = this->allocReg();

                this->current.get().emitCall(
                    MirOp::CALL_FUNC, it->second[node.methodOrdinal], dst, base, -1, argCount, node.loc);
                this->lastResultReg = dst;
            } else {
                this->diagnostics.addError(node.loc, "Unresolved static method call: " + node.name);
            }
            return;
        }

        if (node.isCallable) {
            const int base = this->reserveRegs(argCount);
            for (int i = 0; i < argCount; ++i)
                this->compileArg(base + i, *node.args[i], node.loc);

            const int funcReg = this->emitLoad(node.resolvedName, node.loc);
            const int dst = this->allocReg();

            this->current.get().emitCall(
                MirOp::CALL_LAMBDA, argCount, dst, base, funcReg, argCount, node.loc);
            this->lastResultReg = dst;
            return;
        }

        auto it = this->functionIndices.find(node.resolvedName);
        if (it == this->functionIndices.end() || node.functionOrdinal < 0 ||
            static_cast<size_t>(node.functionOrdinal) >= it->second.size()) {
            this->diagnostics.addError(node.loc, "Unresolved function call: " + node.name);
            return;
        }

        const int base = this->reserveRegs(argCount);
        for (int i = 0; i < argCount; ++i)
            this->compileArg(base + i, *node.args[i], node.loc);

        const int dst = this->allocReg();

        this->current.get().emitCall(
            MirOp::CALL_FUNC, it->second[node.functionOrdinal], dst, base, -1, argCount, node.loc);
        this->lastResultReg = dst;
    }

    void Compiler::visit(LambdaNode& node) {
        int bodyIdx = static_cast<int>(this->chunk.methodBodies.size());
        auto body = std::make_unique<MirChunk>();
        MirChunk& bodyChunk = *body;
        this->chunk.methodBodies.push_back(std::move(body));

        std::reference_wrapper<MirChunk> saved = this->current;
        this->current = std::ref(bodyChunk);
        auto loops = this->suspendLoops();

        this->pushScope(false, this->scopes.back().next, true);
        for (const auto& param : node.decl.params)
            this->declareSlot(param.name);

        int lastReg = -1;
        if (node.decl.body) {
            this->predeclareLocals(*node.decl.body);
            lastReg = this->compilePart(*node.decl.body);
        }

        this->current.get().emit(MirOp::RETURN, 0, -1, lastReg, -1);

        this->current = saved;

        const int captureCount = this->scopes.back().base;
        bodyChunk.slotCount = this->closeScope();

        const int lambdaIdx = this->addLambda(bodyIdx, static_cast<int>(node.decl.params.size()), captureCount);
        const int dst = this->allocReg();

        this->current.get().emitCall(
            MirOp::MAKE_LAMBDA, lambdaIdx, dst, -1, -1, captureCount, node.loc);
        this->lastResultReg = dst;
    }

    void Compiler::visit(ArrayNode& node) {
        const int count = static_cast<int>(node.elements.size());
        const int base = this->reserveRegs(count);

        for (int i = 0; i < count; ++i)
            this->compileArg(base + i, *node.elements[i], node.loc);

        const int dst = this->allocReg();

        this->current.get().emitCall(MirOp::MAKE_ARRAY, count, dst, base, -1, count, node.loc);
        this->lastResultReg = dst;
    }

    void Compiler::visit(IndexAccessNode& node) {
        const int objReg = this->compileValue(*node.target, node.loc);

        if (node.isSafe) {
            const int dst = this->allocReg();
            const int cond = this->allocReg();

            this->current.get().emit(MirOp::IS_NONE, 0, cond, objReg, -1, node.loc);

            const size_t jmpSkipIdx = this->current.get().emit(
                MirOp::JMP_IF_FALSE, 0, -1, cond, -1, node.loc);

            this->current.get().emit(
                MirOp::LOAD_CONST, this->addConstant(std::monostate{}), dst, -1, -1, node.loc);

            const size_t jmpEndIdx = this->current.get().emit(MirOp::JMP, 0, node.loc);

            const int skipTarget = static_cast<int>(this->current.get().currentIP());
            this->current.get().patchJump(jmpSkipIdx, skipTarget - static_cast<int>(jmpSkipIdx) - 1);

            const int idxReg = this->compileValue(*node.index, node.loc);
            this->current.get().emit(MirOp::LOAD_INDEX, 0, dst, objReg, idxReg, node.loc);

            const int endPos = static_cast<int>(this->current.get().currentIP());
            this->current.get().patchJump(jmpEndIdx, endPos - static_cast<int>(jmpEndIdx) - 1);

            this->lastResultReg = dst;
            return;
        }

        const int idxReg = this->compileValue(*node.index, node.loc);
        const int dst = this->allocReg();

        this->current.get().emit(MirOp::LOAD_INDEX, 0, dst, objReg, idxReg, node.loc);
        this->lastResultReg = dst;
    }

}
