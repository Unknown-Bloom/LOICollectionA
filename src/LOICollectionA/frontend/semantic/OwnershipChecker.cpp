#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

#include "LOICollectionA/frontend/semantic/Helpers.h"
#include "LOICollectionA/frontend/semantic/OwnershipChecker.h"

namespace LOICollection::frontend {

    OwnershipChecker::OwnershipChecker(DiagnosticEngine& diag) : diagnostics(diag) {}

    void OwnershipChecker::check(ProgramNode& root) {
        this->bindings.clear();
        this->scopes.clear();
        this->globalBindings.clear();
        this->borrows.clear();
        this->loanTarget.clear();
        this->temporaries.clear();
        this->lambdaDepth = 0;
        this->functionDepth = 0;
        this->reachable = true;

        std::vector<std::reference_wrapper<ClassNode>> classes;
        std::vector<std::reference_wrapper<FunctionDefNode>> functions;

        for (auto& part : root.parts) {
            switch (part->getType()) {
                case ASTNode::Type::Class:
                    classes.push_back(std::ref(static_cast<ClassNode&>(*part)));
                    break;
                case ASTNode::Type::FunctionDef:
                    functions.push_back(std::ref(static_cast<FunctionDefNode&>(*part)));
                    break;
                case ASTNode::Type::Trait:
                case ASTNode::Type::Impl:
                case ASTNode::Type::Enum:
                case ASTNode::Type::Using:
                case ASTNode::Type::Import:
                case ASTNode::Type::Component:
                    break;
                default:
                    this->checkStatement(*part);
                    break;
            }
        }

        for (auto cls : classes)
            this->checkClass(cls.get());

        for (auto fn : functions)
            this->checkFunction(fn.get().decl);
    }

    int OwnershipChecker::findBinding(const std::string& name) const {
        for (auto it = this->scopes.rbegin(); it != this->scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end())
                return found->second;
        }

        auto global = this->globalBindings.find(name);
        return global == this->globalBindings.end() ? -1 : global->second;
    }

    int OwnershipChecker::declare(
        const std::string& name, const TypeInfo& type, SourceLocation loc, bool global) {
        Binding binding;
        binding.name = name;
        binding.type = type;
        binding.lambdaDepth = this->lambdaDepth;
        binding.global = global;
        binding.moveLoc = loc;

        const int id = static_cast<int>(this->bindings.size());
        this->bindings.push_back(std::move(binding));
        this->borrows.push_back(0);

        if (global)
            this->globalBindings[name] = id;
        else if (!this->scopes.empty())
            this->scopes.back()[name] = id;

        return id;
    }

    void OwnershipChecker::pushScope() {
        this->scopes.emplace_back();
    }

    void OwnershipChecker::popScope() {
        if (this->scopes.empty())
            return;

        for (const auto& [name, id] : this->scopes.back()) {
            if (this->bindings[id].type.ownership == Ownership::Borrowed)
                this->releaseLoans(id);
        }

        this->scopes.pop_back();
    }

    void OwnershipChecker::releaseTemporaryBorrows() {
        for (const int id : this->temporaries) {
            if (id >= 0 && static_cast<size_t>(id) < this->borrows.size() &&
                this->borrows[id] > 0)
                --this->borrows[id];
        }

        this->temporaries.clear();
    }

    void OwnershipChecker::addBorrow(int target) {
        if (target < 0 || static_cast<size_t>(target) >= this->borrows.size())
            return;

        ++this->borrows[target];
        this->temporaries.push_back(target);
    }

    void OwnershipChecker::releaseLoans(int refBinding) {
        auto it = this->loanTarget.find(refBinding);
        if (it == this->loanTarget.end())
            return;

        if (it->second >= 0 && static_cast<size_t>(it->second) < this->borrows.size() &&
            this->borrows[it->second] > 0)
            --this->borrows[it->second];

        this->loanTarget.erase(it);
    }

    void OwnershipChecker::acquireLoans(int refBinding, const ExprNode* rhs) {
        if (!rhs)
            return;

        if (rhs->getType() == ASTNode::Type::Unary) {
            const auto& unary = static_cast<const UnaryNode&>(*rhs);
            if (unary.op == "&" && unary.operand) {
                const int target = this->rootBinding(*unary.operand);
                if (target >= 0) {
                    ++this->borrows[target];
                    this->loanTarget[refBinding] = target;
                }
            }
            return;
        }

        if (rhs->getType() == ASTNode::Type::Variable) {
            const auto& var = static_cast<const VariableNode&>(*rhs);
            const int source = this->findBinding(var.name);
            if (source < 0 || this->bindings[source].type.ownership != Ownership::Borrowed)
                return;

            auto loan = this->loanTarget.find(source);
            if (loan == this->loanTarget.end())
                return;

            ++this->borrows[loan->second];
            this->loanTarget[refBinding] = loan->second;
        }
    }

    bool OwnershipChecker::isBorrowed(int id) const {
        return id >= 0 && static_cast<size_t>(id) < this->borrows.size() &&
               this->borrows[id] > 0;
    }

    void OwnershipChecker::useBinding(int id, SourceLocation loc) {
        if (id < 0 || !this->bindings[id].moved)
            return;

        if (this->bindings[id].global && this->functionDepth > 0)
            return;

        this->diagnostics.addError(loc,
            "Use of moved value '" + this->bindings[id].name + "'");
    }

    void OwnershipChecker::moveBinding(int id, SourceLocation loc) {
        Binding& binding = this->bindings[id];

        if (binding.global && this->functionDepth > 0) {
            this->diagnostics.addError(loc,
                "Cannot move global '" + binding.name + "' inside a function; borrow it with '&'");
            return;
        }

        if (this->isBorrowed(id)) {
            this->diagnostics.addError(loc,
                "Cannot move '" + binding.name + "' because it is borrowed");
            return;
        }

        if (binding.moved)
            return;

        binding.moved = true;
        binding.moveLoc = loc;
    }

    int OwnershipChecker::rootBinding(const ExprNode& expr) const {
        switch (expr.getType()) {
            case ASTNode::Type::Variable:
                return this->findBinding(static_cast<const VariableNode&>(expr).name);
            case ASTNode::Type::MemberAccess:
                return this->rootBinding(*static_cast<const MemberAccessNode&>(expr).target);
            case ASTNode::Type::Index:
                return this->rootBinding(*static_cast<const IndexAccessNode&>(expr).target);
            default:
                return -1;
        }
    }

    void OwnershipChecker::movePlace(ExprNode& expr, SourceLocation loc) {
        switch (expr.getType()) {
            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(expr);
                const int id = this->findBinding(var.name);
                if (id < 0)
                    return;

                if (this->bindings[id].type.ownership == Ownership::Borrowed) {
                    this->diagnostics.addError(loc,
                        "Cannot move out of ref<T> '" + var.name + "'");
                    return;
                }

                if (this->bindings[id].type.ownership == Ownership::Owned)
                    this->moveBinding(id, loc);
                return;
            }
            case ASTNode::Type::MemberAccess:
            case ASTNode::Type::Index:
                if (expr.type.ownership == Ownership::Owned)
                    this->diagnostics.addError(loc,
                        "Cannot move an own<T> value out of a field or element");
                return;
            default:
                return;
        }
    }

    OwnershipChecker::Flow OwnershipChecker::snapshot() const {
        Flow flow;
        flow.moved.reserve(this->bindings.size());
        for (const auto& binding : this->bindings)
            flow.moved.push_back(binding.moved);
        flow.reachable = this->reachable;
        return flow;
    }

    void OwnershipChecker::restore(const Flow& flow) {
        for (size_t i = 0; i < this->bindings.size() && i < flow.moved.size(); ++i)
            this->bindings[i].moved = flow.moved[i];

        this->reachable = flow.reachable;
    }

    void OwnershipChecker::mergeBranches(const Flow& before, const Flow& first, const Flow& second) {
        if (!first.reachable && !second.reachable) {
            this->restore(before);
            this->reachable = false;
            return;
        }

        if (!first.reachable) {
            this->restore(second);
            return;
        }

        if (!second.reachable) {
            this->restore(first);
            return;
        }

        for (size_t i = 0; i < this->bindings.size(); ++i) {
            const bool left = i < first.moved.size() && first.moved[i];
            const bool right = i < second.moved.size() && second.moved[i];
            this->bindings[i].moved = left || right;
        }

        this->reachable = true;
    }

    void OwnershipChecker::mergeLoopExit(const Flow& before) {
        for (size_t i = 0; i < this->bindings.size() && i < before.moved.size(); ++i)
            this->bindings[i].moved = this->bindings[i].moved || before.moved[i];

        this->reachable = true;
    }

    void OwnershipChecker::checkMovedInsideLoop(const Flow& before) {
        for (size_t i = 0; i < this->bindings.size(); ++i) {
            Binding& binding = this->bindings[i];
            if (binding.loopReported || !binding.moved)
                continue;

            if (i < before.moved.size() && before.moved[i])
                continue;

            this->diagnostics.addError(binding.moveLoc,
                "Value '" + binding.name +
                "' is moved inside a loop and may be used again on the next iteration");
            binding.loopReported = true;
        }
    }

    void OwnershipChecker::checkSequence(SequenceNode& sequence) {
        for (auto& part : sequence.parts) {
            if (!this->reachable)
                break;

            this->checkStatement(*part);
        }
    }

    void OwnershipChecker::checkStatement(ASTNode& node) {
        if (!this->reachable)
            return;

        switch (node.getType()) {
            case ASTNode::Type::Return: {
                auto& ret = static_cast<ReturnNode&>(node);
                if (ret.value) {
                    this->checkExpr(*ret.value);
                    if (ret.value->type.ownership == Ownership::Owned &&
                        isPlaceExpr(*ret.value))
                        this->movePlace(*ret.value, ret.loc);
                }

                this->reachable = false;
                this->releaseTemporaryBorrows();
                return;
            }
            case ASTNode::Type::Panic: {
                auto& panicNode = static_cast<PanicNode&>(node);
                if (panicNode.message)
                    this->checkExpr(*panicNode.message);

                this->reachable = false;
                this->releaseTemporaryBorrows();
                return;
            }
            case ASTNode::Type::Break:
            case ASTNode::Type::Continue:
                this->reachable = false;
                this->releaseTemporaryBorrows();
                return;
            case ASTNode::Type::Block: {
                this->pushScope();
                this->checkSequence(static_cast<BlockNode&>(node));
                this->popScope();
                break;
            }
            case ASTNode::Type::While:
                this->checkWhile(static_cast<WhileNode&>(node));
                break;
            case ASTNode::Type::For:
                this->checkFor(static_cast<ForNode&>(node));
                break;
            case ASTNode::Type::ForIn:
                this->checkForIn(static_cast<ForInNode&>(node));
                break;
            case ASTNode::Type::If:
                this->checkIf(static_cast<IfNode&>(node));
                break;
            case ASTNode::Type::Match:
                this->checkMatch(static_cast<MatchNode&>(node));
                break;
            case ASTNode::Type::Class:
            case ASTNode::Type::FunctionDef:
            case ASTNode::Type::Trait:
            case ASTNode::Type::Impl:
            case ASTNode::Type::Enum:
            case ASTNode::Type::Using:
            case ASTNode::Type::Import:
            case ASTNode::Type::Component:
                break;
            default:
                this->checkExpr(static_cast<ExprNode&>(node));
                break;
        }

        this->releaseTemporaryBorrows();
    }

    void OwnershipChecker::checkIf(IfNode& node) {
        const bool bindsPattern = node.condition &&
            node.condition->getType() == ASTNode::Type::LetPattern;

        if (bindsPattern)
            this->checkExpr(*static_cast<LetPatternNode&>(*node.condition).value);
        else if (node.condition)
            this->checkExpr(*node.condition);

        const Flow before = this->snapshot();

        this->restore(before);
        this->reachable = true;
        this->pushScope();
        if (bindsPattern) {
            for (const auto& binder : static_cast<LetPatternNode&>(*node.condition).binders)
                this->declare(binder, TypeInfo{}, SourceLocation{}, false);
        }
        if (node.trueBranch)
            this->checkStatement(*node.trueBranch);
        this->popScope();
        const Flow first = this->snapshot();

        Flow second = before;
        second.reachable = true;
        if (node.falseBranch) {
            this->restore(before);
            this->reachable = true;
            this->pushScope();
            this->checkStatement(*node.falseBranch);
            this->popScope();
            second = this->snapshot();
        }

        this->mergeBranches(before, first, second);
    }

    void OwnershipChecker::checkMatch(MatchNode& node) {
        this->checkExpr(*node.target);

        const Flow before = this->snapshot();
        Flow accumulator = before;
        bool any = false;

        for (auto& arm : node.arms) {
            this->restore(before);
            this->reachable = true;

            this->pushScope();
            if (!arm->binder.empty())
                this->declare(arm->binder, TypeInfo{}, SourceLocation{}, false);
            for (const auto& binder : arm->binders)
                this->declare(binder, TypeInfo{}, SourceLocation{}, false);

            if (arm->guard)
                this->checkExpr(*arm->guard);
            if (arm->body)
                this->checkStatement(*arm->body);
            this->popScope();

            const Flow armFlow = this->snapshot();
            if (!any) {
                accumulator = armFlow;
                any = true;
            } else {
                this->restore(accumulator);
                this->mergeBranches(before, accumulator, armFlow);
                accumulator = this->snapshot();
            }
        }

        this->restore(accumulator);
    }

    void OwnershipChecker::checkWhile(WhileNode& node) {
        const Flow before = this->snapshot();
        const bool bindsPattern = node.condition &&
            node.condition->getType() == ASTNode::Type::LetPattern;

        if (bindsPattern)
            this->checkExpr(*static_cast<LetPatternNode&>(*node.condition).value);
        else if (node.condition)
            this->checkExpr(*node.condition);

        this->pushScope();
        if (bindsPattern) {
            for (const auto& binder : static_cast<LetPatternNode&>(*node.condition).binders)
                this->declare(binder, TypeInfo{}, SourceLocation{}, false);
        }
        if (node.body)
            this->checkStatement(*node.body);
        this->popScope();

        this->checkMovedInsideLoop(before);
        this->mergeLoopExit(before);
        this->releaseTemporaryBorrows();
    }

    void OwnershipChecker::checkFor(ForNode& node) {
        const Flow before = this->snapshot();
        this->pushScope();

        if (node.init)
            this->checkExpr(*node.init);
        if (node.condition)
            this->checkExpr(*node.condition);
        if (node.step)
            this->checkExpr(*node.step);
        if (node.body)
            this->checkStatement(*node.body);

        this->popScope();
        this->checkMovedInsideLoop(before);
        this->mergeLoopExit(before);
        this->releaseTemporaryBorrows();
    }

    void OwnershipChecker::checkForIn(ForInNode& node) {
        const Flow before = this->snapshot();
        this->checkExpr(*node.iterable);

        this->pushScope();
        if (node.hasIndexVar)
            this->declare(node.indexVar, TypeInfo{ TypeKind::Int }, SourceLocation{}, false);
        this->declare(node.elementVar, TypeInfo{}, SourceLocation{}, false);

        if (node.body)
            this->checkStatement(*node.body);

        this->popScope();
        this->checkMovedInsideLoop(before);
        this->mergeLoopExit(before);
        this->releaseTemporaryBorrows();
    }

    void OwnershipChecker::checkLambda(LambdaNode& node) {
        ++this->lambdaDepth;
        ++this->functionDepth;
        this->pushScope();

        const bool savedReachable = this->reachable;
        this->reachable = true;

        for (size_t i = 0; i < node.decl.params.size(); ++i) {
            const TypeInfo type = i < node.decl.paramTypes.size()
                ? node.decl.paramTypes[i]
                : TypeInfo{};
            this->declare(node.decl.params[i].name, type, node.decl.loc, false);
        }

        if (node.decl.body)
            this->checkStatement(*node.decl.body);

        this->popScope();

        this->reachable = savedReachable;
        --this->functionDepth;
        --this->lambdaDepth;
    }

    void OwnershipChecker::checkAssignment(AssignmentNode& node) {
        if (node.value)
            this->checkExpr(*node.value);

        const bool variableTarget = node.target->getType() == ASTNode::Type::Variable;
        if (!variableTarget)
            this->checkExpr(*node.target);

        if (!variableTarget)
            return;

        auto& var = static_cast<VariableNode&>(*node.target);

        if (node.isDeclaration) {
            TypeInfo type = node.hasDeclaredType ? node.declaredTypeInfo : TypeInfo{};
            if (!node.hasDeclaredType && node.value)
                type = node.value->type;

            const bool global = this->scopes.empty();
            const int id = this->declare(var.name, type, node.loc, global);

            if (type.ownership == Ownership::Borrowed) {
                this->acquireLoans(id, node.value.get());
                return;
            }

            if (type.ownership == Ownership::Owned && node.value &&
                node.value->type.ownership == Ownership::Owned &&
                isPlaceExpr(*node.value))
                this->movePlace(*node.value, node.loc);
            return;
        }

        const int id = this->findBinding(var.name);
        if (id < 0)
            return;

        if (this->bindings[id].global && this->functionDepth > 0 &&
            this->bindings[id].type.ownership != Ownership::Value) {
            this->diagnostics.addError(node.loc,
                "Cannot assign to global '" + var.name + "' inside a function");
            return;
        }

        if (this->bindings[id].type.ownership == Ownership::Borrowed) {
            this->releaseLoans(id);
            this->acquireLoans(id, node.value.get());
            return;
        }

        if (this->isBorrowed(id)) {
            this->diagnostics.addError(node.loc,
                "Cannot assign to '" + var.name + "' because it is borrowed");
            return;
        }

        if (this->bindings[id].type.ownership == Ownership::Owned && node.value &&
            node.value->type.ownership != Ownership::Owned && isPlaceExpr(*node.value)) {
            this->diagnostics.addError(node.loc,
                "Cannot reassign own<T> '" + var.name +
                "' from an existing value; assign a temporary or move an own<T> value");
            return;
        }

        if (node.value && node.value->type.ownership == Ownership::Owned &&
            isPlaceExpr(*node.value)) {
            if (this->rootBinding(*node.value) == id) {
                this->diagnostics.addError(node.loc,
                    "Cannot move '" + var.name + "' into itself");
                return;
            }

            this->movePlace(*node.value, node.loc);
        }

        if (this->bindings[id].type.ownership == Ownership::Owned) {
            this->bindings[id].moved = false;
            this->bindings[id].loopReported = false;
        }
    }

    void OwnershipChecker::checkCallParams(
        const std::vector<TypeInfo>& paramTypes,
        std::vector<std::unique_ptr<ExprNode>>& args,
        SourceLocation loc,
        const std::string& callee) {
        const size_t count = std::min(paramTypes.size(), args.size());
        for (size_t i = 0; i < count; ++i) {
            const Ownership expected = paramTypes[i].ownership;
            ExprNode& arg = *args[i];

            if (expected == Ownership::Borrowed) {
                if (arg.type.ownership != Ownership::Borrowed)
                    continue;

                if (arg.getType() == ASTNode::Type::Variable) {
                    const int source = this->findBinding(
                        static_cast<VariableNode&>(arg).name);
                    if (source < 0)
                        continue;

                    auto loan = this->loanTarget.find(source);
                    if (loan != this->loanTarget.end())
                        this->addBorrow(loan->second);
                }
                continue;
            }

            if (expected != Ownership::Owned)
                continue;

            if (arg.type.ownership == Ownership::Owned) {
                if (isPlaceExpr(arg))
                    this->movePlace(arg, loc);
                continue;
            }

            if (isPlaceExpr(arg)) {
                const std::string name = arg.getType() == ASTNode::Type::Variable
                    ? static_cast<VariableNode&>(arg).name
                    : std::string("value");
                this->diagnostics.addError(loc,
                    "Call to " + callee + " cannot take ownership of existing " + name +
                    "; declare it as own<T> and move it");
            }
        }
    }

    void OwnershipChecker::checkFunction(MethodDecl& method) {
        this->pushScope();
        ++this->functionDepth;

        const bool savedReachable = this->reachable;
        this->reachable = true;

        for (size_t i = 0; i < method.params.size(); ++i) {
            const TypeInfo type = i < method.paramTypes.size()
                ? method.paramTypes[i]
                : TypeInfo{};
            this->declare(method.params[i].name, type, method.loc, false);
        }

        if (method.body)
            this->checkStatement(*method.body);

        this->popScope();
        --this->functionDepth;
        this->reachable = savedReachable;
    }

    void OwnershipChecker::checkClass(ClassNode& cls) {
        for (auto& method : cls.methods)
            this->checkFunction(method);
    }

    void OwnershipChecker::checkExpr(ExprNode& expr) {
        switch (expr.getType()) {
            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(expr);
                const int id = this->findBinding(var.name);
                if (id < 0)
                    return;

                this->useBinding(id, var.loc);

                const Binding& binding = this->bindings[id];
                if (binding.global || this->lambdaDepth <= binding.lambdaDepth)
                    return;

                if (binding.type.ownership == Ownership::Owned)
                    this->diagnostics.addError(var.loc,
                        "Owned value '" + var.name + "' cannot be captured by a closure");
                else if (binding.type.ownership == Ownership::Borrowed)
                    this->diagnostics.addError(var.loc,
                        "Ref '" + var.name + "' cannot be captured by a closure");
                return;
            }
            case ASTNode::Type::Unary: {
                auto& unary = static_cast<UnaryNode&>(expr);
                if (unary.operand)
                    this->checkExpr(*unary.operand);

                if (unary.op == "&" && unary.operand) {
                    const int target = this->rootBinding(*unary.operand);
                    if (target >= 0)
                        this->addBorrow(target);
                }
                return;
            }
            case ASTNode::Type::Assignment:
                this->checkAssignment(static_cast<AssignmentNode&>(expr));
                return;
            case ASTNode::Type::CompoundAssign: {
                auto& assign = static_cast<CompoundAssignNode&>(expr);
                this->checkExpr(*assign.target);
                this->checkExpr(*assign.value);
                return;
            }
            case ASTNode::Type::Compare: {
                auto& cmp = static_cast<CompareNode&>(expr);
                this->checkExpr(*cmp.left);
                this->checkExpr(*cmp.right);
                return;
            }
            case ASTNode::Type::Logical: {
                auto& logical = static_cast<LogicalNode&>(expr);
                this->checkExpr(*logical.left);
                this->checkExpr(*logical.right);
                return;
            }
            case ASTNode::Type::Arithmetic: {
                auto& arith = static_cast<ArithmeticNode&>(expr);
                this->checkExpr(*arith.left);
                this->checkExpr(*arith.right);
                return;
            }
            case ASTNode::Type::Coalesce: {
                auto& coalesce = static_cast<CoalesceNode&>(expr);
                this->checkExpr(*coalesce.left);
                this->checkExpr(*coalesce.right);
                return;
            }
            case ASTNode::Type::Try:
                this->checkExpr(*static_cast<TryNode&>(expr).operand);
                return;
            case ASTNode::Type::Range: {
                auto& range = static_cast<RangeNode&>(expr);
                this->checkExpr(*range.start);
                this->checkExpr(*range.end);
                return;
            }
            case ASTNode::Type::Function: {
                auto& fn = static_cast<FunctionNode&>(expr);
                for (auto& arg : fn.args)
                    this->checkExpr(*arg);
                this->checkCallParams(fn.paramTypes, fn.args, fn.loc, "static method '" + fn.name + "'");
                return;
            }
            case ASTNode::Type::Macro: {
                auto& macro = static_cast<MacroNode&>(expr);
                for (auto& arg : macro.args)
                    this->checkExpr(*arg);
                return;
            }
            case ASTNode::Type::Array: {
                auto& array = static_cast<ArrayNode&>(expr);
                for (auto& element : array.elements)
                    this->checkExpr(*element);
                return;
            }
            case ASTNode::Type::Index: {
                auto& indexNode = static_cast<IndexAccessNode&>(expr);
                this->checkExpr(*indexNode.target);
                this->checkExpr(*indexNode.index);
                return;
            }
            case ASTNode::Type::MemberAccess:
                this->checkExpr(*static_cast<MemberAccessNode&>(expr).target);
                return;
            case ASTNode::Type::MethodCall: {
                auto& call = static_cast<MethodCallNode&>(expr);
                this->checkExpr(*call.target);
                for (auto& arg : call.args)
                    this->checkExpr(*arg);
                this->checkCallParams(call.paramTypes, call.args, call.loc,
                    "method '" + call.methodName + "'");
                return;
            }
            case ASTNode::Type::FuncCall: {
                auto& call = static_cast<FuncCallNode&>(expr);
                for (auto& arg : call.args)
                    this->checkExpr(*arg);
                this->checkCallParams(call.paramTypes, call.args, call.loc,
                    call.isEnumConstruction
                        ? "enum variant '" + call.name + "'"
                        : "function '" + call.name + "'");
                return;
            }
            case ASTNode::Type::New: {
                auto& newNode = static_cast<NewNode&>(expr);
                for (auto& arg : newNode.args)
                    this->checkExpr(*arg);
                this->checkCallParams(newNode.paramTypes, newNode.args, newNode.loc,
                    "constructor of '" + newNode.className + "'");

                if (newNode.declarativeBlock) {
                    this->pushScope();
                    this->checkSequence(*newNode.declarativeBlock);
                    this->popScope();
                }
                return;
            }
            case ASTNode::Type::SuperCall: {
                auto& call = static_cast<SuperCallNode&>(expr);
                for (auto& arg : call.args)
                    this->checkExpr(*arg);
                this->checkCallParams(call.paramTypes, call.args, call.loc, "base constructor");
                return;
            }
            case ASTNode::Type::Lambda:
                this->checkLambda(static_cast<LambdaNode&>(expr));
                return;
            case ASTNode::Type::If:
                this->checkIf(static_cast<IfNode&>(expr));
                return;
            case ASTNode::Type::Match:
                this->checkMatch(static_cast<MatchNode&>(expr));
                return;
            case ASTNode::Type::While:
                this->checkWhile(static_cast<WhileNode&>(expr));
                return;
            case ASTNode::Type::For:
                this->checkFor(static_cast<ForNode&>(expr));
                return;
            case ASTNode::Type::ForIn:
                this->checkForIn(static_cast<ForInNode&>(expr));
                return;
            case ASTNode::Type::InstanceOf:
                this->checkExpr(*static_cast<InstanceOfNode&>(expr).target);
                return;
            case ASTNode::Type::LetPattern:
                if (static_cast<LetPatternNode&>(expr).value)
                    this->checkExpr(*static_cast<LetPatternNode&>(expr).value);
                return;
            default:
                return;
        }
    }

}
