#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/SemanticAnalyzer.h"

namespace LOICollection::frontend {
    TypeInfo typeFromParam(ParamType param) {
        switch (param) {
            case ParamType::INT: return { TypeKind::Int };
            case ParamType::FLOAT: return { TypeKind::Float };
            case ParamType::STRING: return { TypeKind::String };
            case ParamType::BOOL: return { TypeKind::Bool };
            case ParamType::OBJECT: return { TypeKind::Object };
            case ParamType::FUNCTION: return { TypeKind::Function };
            case ParamType::ARRAY: return { TypeKind::Array };
        }

        return {};
    }

    bool typeMatchesParam(const TypeInfo& type, ParamType param) {
        if (type.kind == TypeKind::Unknown)
            return true;

        if (type.kind == TypeKind::Optional)
            return type.optionalInner && typeMatchesParam(*type.optionalInner, param);

        if (type.kind == TypeKind::Variant) {
            return std::ranges::any_of(type.variantOptions, [param](const TypeInfo& option) -> bool {
                return typeMatchesParam(option, param);
            });
        }

        TypeInfo expected = typeFromParam(param);
        if (expected.kind == TypeKind::Object)
            return type.kind == TypeKind::Object;

        return type == expected;
    }

    bool matchesNativeSignature(const CallbackTypeArgs& signature, const std::vector<TypeInfo>& argTypes) {
        if (signature.size() != argTypes.size())
            return false;

        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (!typeMatchesParam(argTypes[i], signature[i]))
                return false;
        }

        return true;
    }

    bool isNativeClass(const std::string& name) {
        return ClassCall::getInstance().isRegistered(name);
    }

    SemanticAnalyzer::SemanticAnalyzer(DiagnosticEngine& diag) : diagnostics(diag) {}

    void SemanticAnalyzer::analyze(ProgramNode& root) {
        this->classes.clear();
        this->orderedClasses.clear();
        this->classByName.clear();
        this->classMethodOrder.clear();
        this->classMethodOrdinals.clear();
        this->classStaticMethodOrder.clear();
        this->classStaticMethodOrdinals.clear();
        this->functions.clear();
        this->functionsByName.clear();
        this->globalTypes.clear();
        this->declaredGlobals.clear();
        this->aliasExprs.clear();
        this->aliasLocs.clear();
        this->typeAliases.clear();
        this->resolvingAliases.clear();
        this->constructorAssignedMembers.clear();

        this->collectTypeAliases(root);

        for (auto& part : root.parts) {
            switch (part->getType()) {
                case ASTNode::Type::Class:
                    registerClass(static_cast<ClassNode&>(*part));
                    break;
                case ASTNode::Type::FunctionDef:
                    registerFunction(static_cast<FunctionDefNode&>(*part));
                    break;
                default:
                    break;
            }
        }

        resolveHierarchy();
        for (const auto& [name, loc] : this->aliasLocs) {
            if (this->classByName.contains(name)) {
                this->diagnostics.addError(loc,
                    "Type alias conflicts with class name: " + name);
            }
        }

        resolveDeclaredTypes();
        buildMethodOrdinals();

        checkTopLevel(root);
        checkClassBodies();
        checkFunctionBodies();
        validateConstructors();
        validateMemberInitialization();
    }

    void SemanticAnalyzer::resolveHierarchy() {
        std::unordered_set<std::string> visiting;
        std::unordered_set<std::string> visited;

        std::function<void(ClassNode&)> visit = [&](ClassNode& cls) -> void {
            if (visited.contains(cls.name))
                return;

            if (visiting.contains(cls.name)) {
                diagnostics.addError(cls.loc,
                    "Circular inheritance involving class '" + cls.name + "'");
                return;
            }

            visiting.insert(cls.name);

            if (!cls.baseClassName.empty()) {
                auto baseOpt = this->findClass(cls.baseClassName);
                if (!baseOpt) {
                    diagnostics.addError(cls.loc, "Unknown base class: " + cls.baseClassName);
                } else {
                    visit(baseOpt->get());
                }
            }

            visiting.erase(cls.name);
            visited.insert(cls.name);
            this->orderedClasses.push_back(std::ref(cls));
        };

        for (auto cls : this->classes)
            visit(cls.get());
    }

    void SemanticAnalyzer::buildMethodOrdinals() {
        for (auto clsRef : this->orderedClasses) {
            ClassNode& cls = clsRef.get();

            std::vector<std::string> order;
            std::unordered_map<std::string, int> ordinals;
            std::vector<std::string> staticOrder;
            std::unordered_map<std::string, int> staticOrdinals;

            if (!cls.baseClassName.empty()) {
                order = this->classMethodOrder[cls.baseClassName];
                ordinals = this->classMethodOrdinals[cls.baseClassName];
                staticOrder = this->classStaticMethodOrder[cls.baseClassName];
                staticOrdinals = this->classStaticMethodOrdinals[cls.baseClassName];
            }

            for (auto& method : cls.methods) {
                if (method.isConstructor)
                    continue;

                std::string signature = this->methodSignature(method);
                if (method.isStatic) {
                    if (!staticOrdinals.contains(signature)) {
                        staticOrdinals[signature] = static_cast<int>(staticOrder.size());
                        staticOrder.push_back(signature);
                    }
                } else {
                    if (!ordinals.contains(signature)) {
                        ordinals[signature] = static_cast<int>(order.size());
                        order.push_back(signature);
                    }
                }
            }

            this->classMethodOrder[cls.name] = std::move(order);
            this->classMethodOrdinals[cls.name] = std::move(ordinals);
            this->classStaticMethodOrder[cls.name] = std::move(staticOrder);
            this->classStaticMethodOrdinals[cls.name] = std::move(staticOrdinals);
        }
    }

    void SemanticAnalyzer::validateConstructors() {
        for (auto clsRef : this->orderedClasses) {
            ClassNode& cls = clsRef.get();
            if (cls.baseClassName.empty())
                continue;

            auto baseOpt = this->findClass(cls.baseClassName);
            if (!baseOpt)
                continue;

            auto baseCtor = this->findConstructor(baseOpt->get());
            if (!baseCtor || baseCtor->method.get().params.empty())
                continue;

            if (cls.constructorIndex == -1) {
                diagnostics.addError(cls.loc,
                    "Class '" + cls.name + "' must define a constructor to call base constructor with arguments");
            } else if (!cls.methods[cls.constructorIndex].hasSuperCall) {
                diagnostics.addError(cls.loc,
                    "Constructor of class '" + cls.name + "' must call super(...)");
            }
        }
    }

    std::optional<std::reference_wrapper<ClassNode>> SemanticAnalyzer::findClass(const std::string& name) const {
        auto it = classByName.find(name);
        if (it == classByName.end())
            return std::nullopt;

        return it->second;
    }

    TypeInfo SemanticAnalyzer::typeOfValue(const ValueNode::ValueType& value) const {
        switch (value.index()) {
            case 0: return { TypeKind::Int };
            case 1: return { TypeKind::Float };
            case 2: return { TypeKind::String };
            case 3: return { TypeKind::Bool };
            case 4: return { TypeKind::Object };
            case 5: return { TypeKind::Function };
            case 6: return { TypeKind::Array };
            case 7: return { TypeKind::None };
            default: return {};
        }
    }

    TypeInfo SemanticAnalyzer::typeFromName(const std::string& name, SourceLocation loc, bool reportError) const {
        if (name == "int") return { TypeKind::Int };
        if (name == "float") return { TypeKind::Float };
        if (name == "string") return { TypeKind::String };
        if (name == "bool") return { TypeKind::Bool };
        if (name == "void") return { TypeKind::Void };

        if (auto cls = findClass(name))
            return { TypeKind::Object, cls->get().name };

        if (isNativeClass(name))
            return { TypeKind::Object, name };

        if (reportError)
            diagnostics.addError(loc, "Unknown type: " + name);

        return {};
    }

    std::string SemanticAnalyzer::typeToString(const TypeInfo& type) const {
        return typeInfoToString(type);
    }

    bool SemanticAnalyzer::isNumeric(const TypeInfo& type) const {
        return type.kind == TypeKind::Int || type.kind == TypeKind::Float;
    }

    void SemanticAnalyzer::registerClass(ClassNode& node) {
        if (classByName.find(node.name) != classByName.end()) {
            diagnostics.addError(node.loc, "Duplicate class: " + node.name);
            return;
        }

        classByName.emplace(node.name, std::ref(node));
        classes.push_back(std::ref(node));

        std::unordered_set<std::string> memberNames;
        for (const auto& member : node.members) {
            if (!memberNames.insert(member.name).second)
                diagnostics.addError(member.loc, "Duplicate member variable: " + member.name);
        }
    }

    void SemanticAnalyzer::registerFunction(FunctionDefNode& node) {
        functions.push_back(std::ref(node));
        functionsByName[node.name].push_back(std::ref(node));
    }

    namespace {
        bool isReservedTypeName(const std::string& name) {
            return name == "int" || name == "float" || name == "string" ||
                   name == "bool" || name == "void" || name == "variant" ||
                   name == "optional";
        }
    }

    void SemanticAnalyzer::collectTypeAliases(ProgramNode& root) {
        for (auto& part : root.parts) {
            if (part->getType() != ASTNode::Type::Using)
                continue;

            auto& usingNode = static_cast<UsingNode&>(*part);
            if (isReservedTypeName(usingNode.name)) {
                this->diagnostics.addError(usingNode.loc,
                    "Cannot use '" + usingNode.name + "' as a type alias name");
                continue;
            }

            if (!this->aliasExprs.emplace(usingNode.name, usingNode.type).second) {
                this->diagnostics.addError(usingNode.loc,
                    "Duplicate type alias: " + usingNode.name);
                continue;
            }

            this->aliasLocs.emplace(usingNode.name, usingNode.loc);
        }
    }

    TypeInfo SemanticAnalyzer::resolveTypeExpr(const TypeExpr& expr, SourceLocation loc, bool reportError) {
        if (expr.name == "variant") {
            if (expr.args.size() < 2) {
                if (reportError)
                    this->diagnostics.addError(loc, "variant requires at least two type arguments");
                return {};
            }

            TypeInfo result;
            result.kind = TypeKind::Variant;
            result.variantOptions.reserve(expr.args.size());
            for (const auto& arg : expr.args)
                result.variantOptions.push_back(this->resolveTypeExpr(arg, loc, reportError));

            return result;
        }

        if (expr.name == "optional") {
            if (expr.args.size() != 1) {
                if (reportError)
                    this->diagnostics.addError(loc, "optional requires exactly one type argument");
                return {};
            }

            TypeInfo inner = this->resolveTypeExpr(expr.args[0], loc, reportError);
            if (inner.kind == TypeKind::Optional) {
                if (reportError)
                    this->diagnostics.addError(loc, "optional cannot be nested inside optional");
                return {};
            }

            TypeInfo result;
            result.kind = TypeKind::Optional;
            result.optionalInner = std::make_shared<TypeInfo>(inner);
            return result;
        }

        if (!expr.args.empty()) {
            if (reportError)
                this->diagnostics.addError(loc, "Type '" + expr.name + "' does not accept type arguments");
            return {};
        }

        auto aliasIt = this->aliasExprs.find(expr.name);
        if (aliasIt != this->aliasExprs.end()) {
            if (auto resolvedIt = this->typeAliases.find(expr.name);
                resolvedIt != this->typeAliases.end()) {
                return resolvedIt->second;
            }

            if (!this->resolvingAliases.insert(expr.name).second) {
                if (reportError)
                    this->diagnostics.addError(loc,
                        "Circular type alias involving '" + expr.name + "'");
                return {};
            }

            TypeInfo resolved = this->resolveTypeExpr(aliasIt->second, loc, reportError);
            this->resolvingAliases.erase(expr.name);
            this->typeAliases[expr.name] = resolved;
            return resolved;
        }

        return this->typeFromName(expr.name, loc, reportError);
    }

    void SemanticAnalyzer::resolveDeclaredTypes() {
        for (auto clsRef : this->classes) {
            ClassNode& cls = clsRef.get();

            for (auto& member : cls.members) {
                if (member.hasTypeExpr)
                    member.type = this->resolveTypeExpr(member.typeExpr, member.loc, true);

                if (member.hasDefault && member.defaultExpr) {
                    MethodScope emptyScope;
                    TypeInfo defaultType = this->checkExpr(*member.defaultExpr, emptyScope);

                    if (member.type.kind != TypeKind::Unknown) {
                        this->unify(member.type, defaultType, member.loc,
                            "member '" + member.name + "'");
                    } else if (defaultType.kind == TypeKind::None) {
                        this->diagnostics.addError(member.loc,
                            "'None' is only allowed when declaring an optional value (member '" +
                            member.name + "')");
                    }
                }
            }

            for (auto& method : cls.methods) {
                method.paramTypes.resize(method.params.size());
                for (size_t i = 0; i < method.params.size(); ++i) {
                    if (method.params[i].hasType)
                        method.paramTypes[i] = this->resolveTypeExpr(
                            method.params[i].typeExpr, method.loc, true);
                }

                if (method.hasReturnType)
                    method.returnType = this->resolveTypeExpr(
                        method.returnTypeExpr, method.loc, true);
            }
        }

        for (auto fnRef : this->functions) {
            MethodDecl& decl = fnRef.get().decl;
            decl.paramTypes.resize(decl.params.size());
            for (size_t i = 0; i < decl.params.size(); ++i) {
                if (decl.params[i].hasType)
                    decl.paramTypes[i] = this->resolveTypeExpr(
                        decl.params[i].typeExpr, decl.loc, true);
            }

            if (decl.hasReturnType)
                decl.returnType = this->resolveTypeExpr(decl.returnTypeExpr, decl.loc, true);
        }
    }

    void SemanticAnalyzer::validateMemberInitialization() {
        for (auto clsRef : this->classes) {
            ClassNode& cls = clsRef.get();
            const auto& assigned = this->constructorAssignedMembers[cls.name];

            for (const auto& member : cls.members) {
                if (member.isStatic || member.hasDefault)
                    continue;

                if (cls.constructorIndex == -1) {
                    this->diagnostics.addError(member.loc,
                        "Member '" + member.name + "' has no default value and class '" +
                        cls.name + "' has no constructor to initialize it");
                } else if (!assigned.contains(member.name)) {
                    this->diagnostics.addError(member.loc,
                        "Member '" + member.name + "' has no default value and is not assigned " +
                        "in the constructor of class '" + cls.name + "'");
                }
            }
        }
    }

    void SemanticAnalyzer::checkTopLevel(ProgramNode& root) {
        MethodScope emptyScope;

        for (auto& part : root.parts) {
            switch (part->getType()) {
                case ASTNode::Type::Class:
                case ASTNode::Type::FunctionDef:
                case ASTNode::Type::Using:
                    continue;
                default:
                    checkStatement(*part, emptyScope);
            }
        }
    }

    void SemanticAnalyzer::checkClassBodies() {
        for (auto cls : classes)
            checkClassBody(cls.get());
    }

    void SemanticAnalyzer::checkFunctionBodies() {
        for (auto fn : functions)
            checkBody(std::nullopt, fn.get().decl);
    }

    void SemanticAnalyzer::checkClassBody(ClassNode& cls) {
        for (auto& method : cls.methods)
            checkBody(std::ref(cls), method);
    }

    void SemanticAnalyzer::checkBody(std::optional<std::reference_wrapper<ClassNode>> cls, MethodDecl& method) {
        MethodScope scope{ cls, std::ref(method) };

        if (method.body)
            checkStatement(*method.body, scope);

        if (method.hasReturnType && !method.isConstructor &&
            method.returnType.kind != TypeKind::Void && !method.hasReturnStatement) {
            diagnostics.addWarning(method.loc,
                "Missing return statement in function '" + method.name + "'");
        }
    }

    void SemanticAnalyzer::checkStatement(ASTNode& node, MethodScope& scope) {
        switch (node.getType()) {
            case ASTNode::Type::Class:
            case ASTNode::Type::FunctionDef:
            case ASTNode::Type::Using:
                return;
            case ASTNode::Type::Return:
                checkReturn(static_cast<ReturnNode&>(node), scope);
                return;
            case ASTNode::Type::While: {
                auto& whileNode = static_cast<WhileNode&>(node);
                if (whileNode.condition)
                    checkExpr(*whileNode.condition, scope);
                if (whileNode.body)
                    checkStatement(*whileNode.body, scope);
                return;
            }
            case ASTNode::Type::For: {
                auto& forNode = static_cast<ForNode&>(node);
                if (forNode.init)
                    checkExpr(*forNode.init, scope);
                if (forNode.condition)
                    checkExpr(*forNode.condition, scope);
                if (forNode.step)
                    checkExpr(*forNode.step, scope);
                if (forNode.body)
                    checkStatement(*forNode.body, scope);
                return;
            }
            case ASTNode::Type::ForIn: {
                auto& forIn = static_cast<ForInNode&>(node);
                TypeInfo iterableType = checkExpr(*forIn.iterable, scope);

                bool isRange = forIn.iterable->getType() == ASTNode::Type::Range;
                if (!isRange && iterableType.kind != TypeKind::Unknown &&
                    iterableType.kind != TypeKind::Array) {
                    this->diagnostics.addError(forIn.loc, "for-in iterable must be an array");
                }

                if (forIn.hasIndexVar)
                    this->globalTypes[forIn.indexVar] = { TypeKind::Int };
                this->globalTypes[forIn.elementVar] = isRange
                    ? TypeInfo{ TypeKind::Int }
                    : TypeInfo{};

                if (forIn.body)
                    checkStatement(*forIn.body, scope);
                return;
            }
            case ASTNode::Type::Break:
            case ASTNode::Type::Continue:
                return;
            case ASTNode::Type::Block:
                for (auto& part : static_cast<BlockNode&>(node).parts)
                    checkStatement(*part, scope);
                return;
            default: {
                TypeInfo type = checkExpr(static_cast<ExprNode&>(node), scope);
                if (type.kind == TypeKind::None &&
                    node.getType() != ASTNode::Type::Assignment) {
                    this->diagnostics.addError({0, 0, 0},
                        "'None' is only allowed when assigning to an optional value");
                }
                return;
            }
        }
    }

    TypeInfo SemanticAnalyzer::checkExpr(ExprNode& node, MethodScope& scope) {
        node.preserveOptional = false;
        TypeInfo result = this->checkExprImpl(node, scope);
        node.type = result;
        return result;
    }

    TypeInfo SemanticAnalyzer::checkExprImpl(ExprNode& node, MethodScope& scope) {
        switch (node.getType()) {
            case ASTNode::Type::Assignment:
                return checkAssignment(static_cast<AssignmentNode&>(node), scope);

            case ASTNode::Type::CompoundAssign: {
                auto& assign = static_cast<CompoundAssignNode&>(node);
                TypeInfo target = checkExpr(*assign.target, scope);
                TypeInfo value = checkExpr(*assign.value, scope);

                if (assign.op == "+" && (target.kind == TypeKind::String || value.kind == TypeKind::String))
                    return { TypeKind::String };

                if (isNumeric(target) && isNumeric(value)) {
                    if (target.kind == TypeKind::Int && value.kind == TypeKind::Int)
                        return { TypeKind::Int };

                    return { TypeKind::Float };
                }

                return {};
            }

            case ASTNode::Type::Coalesce: {
                auto& coalesce = static_cast<CoalesceNode&>(node);
                TypeInfo left = checkExpr(*coalesce.left, scope);
                TypeInfo right = checkExpr(*coalesce.right, scope);

                // None / empty optional is meaningful for '??': keep the raw value.
                coalesce.left->preserveOptional = true;

                while (left.kind == TypeKind::Optional)
                    left = *left.optionalInner;

                if (left.kind == TypeKind::Unknown || right.kind == TypeKind::Unknown)
                    return {};
                if (left.kind == right.kind)
                    return left;

                return {};
            }

            case ASTNode::Type::Range: {
                auto& range = static_cast<RangeNode&>(node);
                TypeInfo start = checkExpr(*range.start, scope);
                TypeInfo end = checkExpr(*range.end, scope);

                if (start.kind != TypeKind::Unknown && start.kind != TypeKind::Int)
                    this->diagnostics.addError(range.loc, "Range start must be an int");
                if (end.kind != TypeKind::Unknown && end.kind != TypeKind::Int)
                    this->diagnostics.addError(range.loc, "Range end must be an int");

                return { TypeKind::Int };
            }

            case ASTNode::Type::Value:
                return typeOfValue(static_cast<ValueNode&>(node).value);

            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(node);
                TypeInfo type = lookupName(var.name, scope);

                if (scope.hasClass()) {
                    bool isParam = false;
                    if (scope.hasMethod()) {
                        for (const auto& param : scope.methodRef().params) {
                            if (param.name == var.name) {
                                isParam = true;
                                break;
                            }
                        }
                    }

                    if (isParam)
                        return type;

                    if (!scope.hasMethod() || !scope.methodRef().isStatic) {
                        if (auto field = findField(scope.classRef(), var.name)) {
                            if (field->member.get().isPrivate && &scope.classRef() != &field->owner.get()) {
                                diagnostics.addError(var.loc,
                                    "Cannot access private member '" + var.name + "'");
                            }

                            return type;
                        }
                    }

                    if (auto staticField = findStaticField(scope.classRef(), var.name)) {
                        if (staticField->member.get().isPrivate && &scope.classRef() != &staticField->owner.get()) {
                            diagnostics.addError(var.loc,
                                "Cannot access private member '" + var.name + "'");
                        }

                        var.isStaticField = true;
                        var.staticClassName = staticField->owner.get().name;
                        return type;
                    }
                }

                return type;
            }

            case ASTNode::Type::This: {
                auto& self = static_cast<ThisNode&>(node);
                if (!scope.hasMethod() || !scope.hasClass()) {
                    diagnostics.addError(self.loc, "'this' is only available inside class methods");
                    return {};
                }

                if (scope.methodRef().isStatic) {
                    diagnostics.addError(self.loc, "'this' is not available inside static methods");
                    return {};
                }

                return { TypeKind::Object, scope.classRef().name };
            }

            case ASTNode::Type::Super: {
                auto& superNode = static_cast<SuperNode&>(node);
                if (!scope.hasMethod() || !scope.hasClass()) {
                    diagnostics.addError(superNode.loc, "'super' is only available inside class methods");
                    return {};
                }

                if (scope.methodRef().isStatic) {
                    diagnostics.addError(superNode.loc, "'super' is not available inside static methods");
                    return {};
                }

                ClassNode& cls = scope.classRef();
                if (cls.baseClassName.empty()) {
                    diagnostics.addError(superNode.loc, "Class '" + cls.name + "' has no base class");
                    return {};
                }

                return { TypeKind::Object, cls.baseClassName };
            }

            case ASTNode::Type::SuperCall:
                return checkSuperCall(static_cast<SuperCallNode&>(node), scope);

            case ASTNode::Type::InstanceOf:
                return checkInstanceOf(static_cast<InstanceOfNode&>(node), scope);

            case ASTNode::Type::New:
                return checkNew(static_cast<NewNode&>(node), scope);

            case ASTNode::Type::MemberAccess:
                return checkMemberAccess(static_cast<MemberAccessNode&>(node), scope);

            case ASTNode::Type::MethodCall:
                return checkMethodCall(static_cast<MethodCallNode&>(node), scope);

            case ASTNode::Type::FuncCall:
                return checkFuncCall(static_cast<FuncCallNode&>(node), scope);

            case ASTNode::Type::Lambda:
                return checkLambda(static_cast<LambdaNode&>(node), scope);

            case ASTNode::Type::Array: {
                auto& array = static_cast<ArrayNode&>(node);
                for (auto& element : array.elements)
                    checkExpr(*element, scope);

                return { TypeKind::Array };
            }

            case ASTNode::Type::Index: {
                auto& indexNode = static_cast<IndexAccessNode&>(node);
                TypeInfo targetType = checkExpr(*indexNode.target, scope);
                TypeInfo indexType = checkExpr(*indexNode.index, scope);

                if (targetType.kind == TypeKind::Unknown)
                    return {};

                if (targetType.kind != TypeKind::Array) {
                    diagnostics.addError(indexNode.loc, "Cannot index a non-array value");
                    return {};
                }

                if (indexType.kind != TypeKind::Unknown && indexType.kind != TypeKind::Int) {
                    diagnostics.addError(indexNode.loc, "Array index must be an int");
                }

                return {};
            }

            case ASTNode::Type::If: {
                auto& ifNode = static_cast<IfNode&>(node);
                if (ifNode.condition)
                    checkExpr(*ifNode.condition, scope);
                if (ifNode.trueBranch)
                    checkStatement(*ifNode.trueBranch, scope);
                if (ifNode.falseBranch)
                    checkStatement(*ifNode.falseBranch, scope);

                return {};
            }

            case ASTNode::Type::Arithmetic: {
                auto& arith = static_cast<ArithmeticNode&>(node);
                TypeInfo left = checkExpr(*arith.left, scope);
                TypeInfo right = checkExpr(*arith.right, scope);

                if (arith.op == "+" && (left.kind == TypeKind::String || right.kind == TypeKind::String))
                    return { TypeKind::String };

                if (isNumeric(left) && isNumeric(right)) {
                    if ((arith.op == "+" || arith.op == "-" || arith.op == "*") &&
                        left.kind == TypeKind::Int && right.kind == TypeKind::Int)
                        return { TypeKind::Int };

                    if (arith.op == "%" && left.kind == TypeKind::Int && right.kind == TypeKind::Int)
                        return { TypeKind::Int };

                    return { TypeKind::Float };
                }

                return {};
            }

            case ASTNode::Type::Compare: {
                auto& cmp = static_cast<CompareNode&>(node);
                checkExpr(*cmp.left, scope);
                checkExpr(*cmp.right, scope);
                return { TypeKind::Bool };
            }

            case ASTNode::Type::Logical: {
                auto& logical = static_cast<LogicalNode&>(node);
                checkExpr(*logical.left, scope);
                checkExpr(*logical.right, scope);
                return { TypeKind::Bool };
            }

            case ASTNode::Type::Unary: {
                auto& unary = static_cast<UnaryNode&>(node);
                TypeInfo operand = checkExpr(*unary.operand, scope);
                return unary.op == "!" ? TypeInfo{ TypeKind::Bool } : operand;
            }

            case ASTNode::Type::Function:
                for (auto& arg : static_cast<FunctionNode&>(node).args)
                    checkExpr(*arg, scope);
                return {};

            case ASTNode::Type::Macro:
                for (auto& arg : static_cast<MacroNode&>(node).args)
                    checkExpr(*arg, scope);
                return {};

            default:
                return {};
        }
    }

    TypeInfo SemanticAnalyzer::checkAssignment(AssignmentNode& node, MethodScope& scope) {
        TypeInfo rhs;
        if (node.value)
            rhs = checkExpr(*node.value, scope);

        switch (node.target->getType()) {
            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(*node.target);
                if (scope.hasMethod()) {
                    MethodDecl& method = scope.methodRef();
                    for (size_t i = 0; i < method.params.size(); ++i) {
                        if (method.params[i].name == var.name) {
                            if (method.params[i].hasType) {
                                unify(method.paramTypes[i], rhs, node.loc, "parameter '" + var.name + "'");
                                if (method.paramTypes[i].kind == TypeKind::Optional &&
                                    rhs.kind == TypeKind::Optional && node.value) {
                                    node.value->preserveOptional = true;
                                }
                            }
                            return rhs;
                        }
                    }

                    if (scope.hasClass() && !scope.methodRef().isStatic) {
                        if (auto field = findField(scope.classRef(), var.name)) {
                            ClassMember& member = field->member.get();
                            if (member.isPrivate && &scope.classRef() != &field->owner.get()) {
                                diagnostics.addError(node.loc,
                                    "Cannot access private member '" + member.name + "'");
                            }

                            if (member.type.kind != TypeKind::Unknown) {
                                unify(member.type, rhs, node.loc, "member '" + var.name + "'");
                                if (member.type.kind == TypeKind::Optional &&
                                    rhs.kind == TypeKind::Optional && node.value) {
                                    node.value->preserveOptional = true;
                                }
                            }

                            if (scope.methodRef().isConstructor)
                                this->constructorAssignedMembers[scope.classRef().name].insert(var.name);
                            return rhs;
                        }
                    }

                    if (scope.hasClass()) {
                        if (auto staticField = findStaticField(scope.classRef(), var.name)) {
                            ClassMember& member = staticField->member.get();
                            if (member.isPrivate && &scope.classRef() != &staticField->owner.get()) {
                                diagnostics.addError(node.loc,
                                    "Cannot access private member '" + member.name + "'");
                            }

                            if (member.type.kind != TypeKind::Unknown) {
                                unify(member.type, rhs, node.loc, "static member '" + var.name + "'");
                                if (member.type.kind == TypeKind::Optional &&
                                    rhs.kind == TypeKind::Optional && node.value) {
                                    node.value->preserveOptional = true;
                                }
                            }

                            var.isStaticField = true;
                            var.staticClassName = staticField->owner.get().name;
                            return rhs;
                        }
                    }
                }

                if (node.hasDeclaredType) {
                    TypeInfo declared = this->resolveTypeExpr(node.declaredType, node.loc, true);

                    if (auto existing = this->declaredGlobals.find(var.name);
                        existing != this->declaredGlobals.end()) {
                        if (!(existing->second == declared)) {
                            this->diagnostics.addError(node.loc,
                                "Conflicting type declaration for variable '" + var.name + "'");
                        }
                    } else if (this->globalTypes.contains(var.name)) {
                        this->diagnostics.addError(node.loc,
                            "Variable '" + var.name + "' was already defined without an explicit type");
                    } else {
                        this->declaredGlobals[var.name] = declared;
                    }

                    if (!node.value) {
                        this->diagnostics.addError(node.loc,
                            "Typed declaration of '" + var.name + "' requires an initializer");
                        return rhs;
                    }

                    auto declaredIt = this->declaredGlobals.find(var.name);
                    if (declaredIt == this->declaredGlobals.end())
                        return rhs;

                    TypeInfo& target = declaredIt->second;
                    this->unify(target, rhs, node.loc, "variable '" + var.name + "'");
                    if (target.kind == TypeKind::Optional &&
                        rhs.kind == TypeKind::Optional && node.value) {
                        node.value->preserveOptional = true;
                    }
                    return rhs;
                }

                if (auto declaredIt = this->declaredGlobals.find(var.name);
                    declaredIt != this->declaredGlobals.end()) {
                    this->unify(declaredIt->second, rhs, node.loc,
                        "variable '" + var.name + "'");
                    if (declaredIt->second.kind == TypeKind::Optional &&
                        rhs.kind == TypeKind::Optional && node.value) {
                        node.value->preserveOptional = true;
                    }
                    return rhs;
                }

                if (rhs.kind == TypeKind::None) {
                    // Dynamic variables may hold 'None' (e.g. before a '??' fallback);
                    // the inferred type stays dynamic so later assignments remain valid.
                    globalTypes[var.name] = TypeInfo{};
                    return rhs;
                }

                if (rhs.kind != TypeKind::Unknown)
                    globalTypes[var.name] = rhs;
                else
                    globalTypes.try_emplace(var.name, rhs);

                return rhs;
            }

            case ASTNode::Type::MemberAccess: {
                auto& member = static_cast<MemberAccessNode&>(*node.target);

                if (member.target->getType() == ASTNode::Type::Variable) {
                    auto& var = static_cast<VariableNode&>(*member.target);
                    if (auto clsOpt = this->findClass(var.name)) {
                        auto field = this->findStaticField(clsOpt->get(), member.memberName);
                        if (!field) {
                            diagnostics.addError(node.loc,
                                "Class '" + clsOpt->get().name + "' has no static member '" +
                                member.memberName + "'");
                            return rhs;
                        }

                        ClassMember& m = field->member.get();
                        if (m.isPrivate &&
                            !(scope.hasMethod() && &scope.classRef() == &field->owner.get())) {
                            diagnostics.addError(node.loc,
                                "Cannot access private member '" + m.name + "'");
                        }

                        if (m.type.kind != TypeKind::Unknown) {
                            unify(m.type, rhs, node.loc, "static member '" + m.name + "'");
                            if (m.type.kind == TypeKind::Optional &&
                                rhs.kind == TypeKind::Optional && node.value) {
                                node.value->preserveOptional = true;
                            }
                        }

                        member.isStaticAccess = true;
                        member.staticClassName = field->owner.get().name;
                        return rhs;
                    }

                    if (isNativeClass(var.name)) {
                        if (ClassCall::getInstance().hasStaticField(var.name, member.memberName)) {
                            member.isStaticAccess = true;
                            member.staticClassName = var.name;
                            return rhs;
                        }

                        diagnostics.addError(node.loc,
                            "Native class '" + var.name + "' has no static member '" +
                            member.memberName + "'");
                        return rhs;
                    }
                }

                TypeInfo targetType = checkExpr(*member.target, scope);

                if (targetType.kind == TypeKind::Unknown)
                    return rhs;

                if (targetType.kind == TypeKind::Optional)
                    targetType = *targetType.optionalInner;

                if (targetType.kind != TypeKind::Object) {
                    diagnostics.addError(node.loc, "Cannot access member of a non-object value");
                    return rhs;
                }

                auto clsOpt = findClass(targetType.className);
                if (!clsOpt) {
                    if (isNativeClass(targetType.className)) {
                        if (ClassCall::getInstance().hasField(targetType.className, member.memberName)) {
                            return rhs;
                        }

                        diagnostics.addError(node.loc,
                            "Class '" + targetType.className + "' has no member '" + member.memberName + "'");
                        return rhs;
                    }

                    diagnostics.addError(node.loc, "Unknown class: " + targetType.className);
                    return rhs;
                }

                ClassNode& cls = clsOpt->get();
                if (auto field = findField(cls, member.memberName)) {
                    ClassMember& m = field->member.get();
                    if (m.isPrivate && !(scope.hasMethod() && &scope.classRef() == &field->owner.get())) {
                        diagnostics.addError(node.loc, "Cannot access private member '" + m.name + "'");
                    }

                    if (m.type.kind != TypeKind::Unknown) {
                        unify(m.type, rhs, node.loc, "member '" + m.name + "'");
                        if (m.type.kind == TypeKind::Optional &&
                            rhs.kind == TypeKind::Optional && node.value) {
                            node.value->preserveOptional = true;
                        }
                    }

                    if (scope.hasMethod() && scope.methodRef().isConstructor &&
                        member.target->getType() == ASTNode::Type::This) {
                        this->constructorAssignedMembers[scope.classRef().name].insert(member.memberName);
                    }
                    return rhs;
                }

                diagnostics.addError(node.loc,
                    "Class '" + cls.name + "' has no member '" + member.memberName + "'");
                return rhs;
            }

            case ASTNode::Type::Index: {
                auto& indexNode = static_cast<IndexAccessNode&>(*node.target);
                TypeInfo targetType = checkExpr(*indexNode.target, scope);
                TypeInfo indexType = checkExpr(*indexNode.index, scope);

                if (targetType.kind == TypeKind::Unknown)
                    return rhs;

                if (targetType.kind != TypeKind::Array) {
                    diagnostics.addError(node.loc, "Cannot assign to an index of a non-array value");
                    return rhs;
                }

                if (indexType.kind != TypeKind::Unknown && indexType.kind != TypeKind::Int) {
                    diagnostics.addError(node.loc, "Array index must be an int");
                }

                return rhs;
            }

            default:
                diagnostics.addError(node.loc, "Invalid assignment target");
                return rhs;
        }
    }

    TypeInfo SemanticAnalyzer::checkMemberAccess(MemberAccessNode& node, MethodScope& scope) {
        if (node.target->getType() == ASTNode::Type::Variable) {
            auto& var = static_cast<VariableNode&>(*node.target);
            if (auto clsOpt = this->findClass(var.name)) {
                auto field = this->findStaticField(clsOpt->get(), node.memberName);
                if (!field) {
                    diagnostics.addError(node.loc,
                        "Class '" + clsOpt->get().name + "' has no static member '" +
                        node.memberName + "'");
                    return {};
                }

                ClassMember& m = field->member.get();
                if (m.isPrivate &&
                    !(scope.hasMethod() && &scope.classRef() == &field->owner.get())) {
                    diagnostics.addError(node.loc,
                        "Cannot access private member '" + m.name + "' of class '" +
                        field->owner.get().name + "'");
                }

                node.isStaticAccess = true;
                node.staticClassName = field->owner.get().name;
                return m.type;
            }

            if (isNativeClass(var.name)) {
                if (ClassCall::getInstance().hasStaticField(var.name, node.memberName)) {
                    node.isStaticAccess = true;
                    node.staticClassName = var.name;
                    return {};
                }

                diagnostics.addError(node.loc,
                    "Native class '" + var.name + "' has no static member '" +
                    node.memberName + "'");
                return {};
            }
        }

        TypeInfo targetType = checkExpr(*node.target, scope);

        bool safeMaybeNone = false;
        if (node.isSafe) {
            // None / empty optional short-circuits a safe chain: keep the raw target.
            node.target->preserveOptional = true;
            safeMaybeNone = targetType.kind == TypeKind::Optional ||
                            targetType.kind == TypeKind::Unknown;
        }

        TypeInfo result = this->checkMemberAccessImpl(node, scope, targetType);

        if (!node.isSafe || !safeMaybeNone)
            return result;

        if (result.kind == TypeKind::Unknown || result.kind == TypeKind::None)
            return {};

        node.preserveOptional = true;
        TypeInfo optional;
        optional.kind = TypeKind::Optional;
        optional.optionalInner = std::make_shared<TypeInfo>(result);
        return optional;
    }

    TypeInfo SemanticAnalyzer::checkMemberAccessImpl(MemberAccessNode& node, MethodScope& scope, TypeInfo targetType) {
        if (targetType.kind == TypeKind::Unknown)
            return {};

        if (targetType.kind == TypeKind::Variant || targetType.kind == TypeKind::Optional) {
            if (node.memberName == "type") {
                node.memberKind = MemberAccessNode::MemberKind::TypeOf;
                node.target->preserveOptional = true;
                return { TypeKind::String };
            }

            if (node.memberName == "value") {
                node.memberKind = MemberAccessNode::MemberKind::Value;
                node.target->preserveOptional = true;

                if (targetType.kind == TypeKind::Optional)
                    return *targetType.optionalInner;

                TypeInfo result;
                for (const auto& option : targetType.variantOptions) {
                    if (result.kind == TypeKind::Unknown)
                        result = option;
                    else if (!(result == option))
                        return {};
                }
                return result;
            }

            if (node.memberName == "has_value") {
                if (targetType.kind != TypeKind::Optional) {
                    this->diagnostics.addError(node.loc,
                        "'.has_value' is only available on optional values");
                    return {};
                }

                node.memberKind = MemberAccessNode::MemberKind::HasValue;
                node.target->preserveOptional = true;
                return { TypeKind::Bool };
            }

            if (targetType.kind == TypeKind::Variant) {
                this->diagnostics.addError(node.loc,
                    "Variant has no member '" + node.memberName + "'");
                return {};
            }
        }

        while (targetType.kind == TypeKind::Optional)
            targetType = *targetType.optionalInner;

        if (targetType.kind == TypeKind::Array) {
            if (node.memberName == "length")
                return { TypeKind::Int };

            diagnostics.addError(node.loc, "Array has no member '" + node.memberName + "'");
            return {};
        }

        if (targetType.kind == TypeKind::String) {
            if (node.memberName == "length")
                return { TypeKind::Int };

            diagnostics.addError(node.loc, "String has no member '" + node.memberName + "'");
            return {};
        }

        if (targetType.kind != TypeKind::Object) {
            diagnostics.addError(node.loc, "Cannot access member of a non-object value");
            return {};
        }

        auto clsOpt = findClass(targetType.className);
        if (!clsOpt) {
            if (isNativeClass(targetType.className)) {
                if (!ClassCall::getInstance().hasField(targetType.className, node.memberName)) {
                    diagnostics.addError(node.loc,
                        "Class '" + targetType.className + "' has no member '" + node.memberName + "'");
                }

                return {};
            }

            diagnostics.addError(node.loc, "Unknown class: " + targetType.className);
            return {};
        }

        ClassNode& cls = clsOpt->get();
        if (auto field = findField(cls, node.memberName)) {
            ClassMember& m = field->member.get();
            if (m.isPrivate && !(scope.hasMethod() && &scope.classRef() == &field->owner.get())) {
                diagnostics.addError(node.loc,
                    "Cannot access private member '" + m.name + "' of class '" + field->owner.get().name + "'");
            }

            return m.type;
        }

        diagnostics.addError(node.loc,
            "Class '" + cls.name + "' has no member '" + node.memberName + "'");
        return {};
    }

    TypeInfo SemanticAnalyzer::checkMethodCall(MethodCallNode& node, MethodScope& scope) {
        size_t argCount = node.args.size();

        std::vector<TypeInfo> argTypes;
        argTypes.reserve(argCount);
        for (auto& arg : node.args)
            argTypes.push_back(checkExpr(*arg, scope));

        if (node.target->getType() == ASTNode::Type::Variable) {
            auto& var = static_cast<VariableNode&>(*node.target);
            if (auto clsOpt = this->findClass(var.name)) {
                auto staticMethod = this->findStaticMethod(
                    clsOpt->get(), node.methodName, argTypes, scope
                );
                if (!staticMethod) {
                    diagnostics.addError(node.loc,
                        "No matching static method '" + node.methodName + "' with " +
                        std::to_string(argCount) + " argument(s) in class '" +
                        clsOpt->get().name + "'");
                    return {};
                }

                MethodDecl& method = staticMethod->method.get();
                node.isStaticCall = true;
                node.staticClassName = clsOpt->get().name;
                node.methodOrdinal = this->staticMethodOrdinal(
                    clsOpt->get().name, this->methodSignature(method)
                );

                for (size_t j = 0; j < argCount; ++j) {
                    if (method.paramTypes[j].kind == TypeKind::Optional &&
                        argTypes[j].kind == TypeKind::Optional) {
                        node.args[j]->preserveOptional = true;
                    }
                }

                return method.returnType;
            }

            if (isNativeClass(var.name)) {
                std::vector<CallbackTypeArgs> signatures =
                    ClassCall::getInstance().getStaticMethodSignatures(var.name, node.methodName);

                for (const auto& signature : signatures) {
                    if (matchesNativeSignature(signature, argTypes)) {
                        node.isStaticCall = true;
                        node.staticClassName = var.name;
                        return {};
                    }
                }

                diagnostics.addError(node.loc,
                    "No matching static method '" + node.methodName + "' with " +
                    std::to_string(argCount) + " argument(s) in native class '" +
                    var.name + "'");
                return {};
            }
        }

        TypeInfo targetType = checkExpr(*node.target, scope);

        while (targetType.kind == TypeKind::Optional)
            targetType = *targetType.optionalInner;

        if (targetType.kind == TypeKind::Array || targetType.kind == TypeKind::String) {
            const std::string& className = targetType.kind == TypeKind::Array ? "Array" : "String";
            std::vector<CallbackTypeArgs> signatures =
                ClassCall::getInstance().getValueMethodSignatures(className, node.methodName);

            for (const auto& signature : signatures) {
                if (matchesNativeSignature(signature, argTypes)) {
                    node.className = className;
                    node.methodOrdinal = -1;
                    return {};
                }
            }

            diagnostics.addError(node.loc,
                "No matching method '" + node.methodName + "' with " +
                std::to_string(argCount) + " argument(s) on " + className + " values");
            return {};
        }

        if (targetType.kind == TypeKind::Unknown) {
            /* Dynamically typed target (e.g. a member read of an object or native
             * instance): defer the value-method dispatch to the VM, which selects
             * the value class by the runtime type of the receiver. */
            for (const char* className : { "Array", "String" }) {
                std::vector<CallbackTypeArgs> signatures =
                    ClassCall::getInstance().getValueMethodSignatures(className, node.methodName);

                for (const auto& signature : signatures) {
                    if (matchesNativeSignature(signature, argTypes)) {
                        node.className = className;
                        node.methodOrdinal = -1;
                        return {};
                    }
                }
            }
        }

        if (targetType.kind != TypeKind::Object) {
            diagnostics.addError(node.loc, "Method call target is not an object");
            return {};
        }

        auto clsOpt = findClass(targetType.className);
        if (!clsOpt) {
            if (isNativeClass(targetType.className)) {
                std::vector<CallbackTypeArgs> signatures =
                    ClassCall::getInstance().getMethodSignatures(targetType.className, node.methodName);

                for (const auto& signature : signatures) {
                    if (matchesNativeSignature(signature, argTypes)) {
                        node.className = targetType.className;
                        node.methodOrdinal = -1;
                        return {};
                    }
                }

                diagnostics.addError(node.loc,
                    "No matching method '" + node.methodName + "' with " +
                    std::to_string(argCount) + " argument(s) in native class '" +
                    targetType.className + "'");
                return {};
            }

            diagnostics.addError(node.loc, "Unknown class: " + targetType.className);
            return {};
        }

        ClassNode& cls = clsOpt->get();

        std::vector<std::pair<std::reference_wrapper<ClassNode>, std::reference_wrapper<MethodDecl>>> candidates;
        std::optional<std::reference_wrapper<ClassNode>> walk = std::ref(cls);
        for (size_t step = 0; step <= this->classes.size() && walk; ++step) {
            ClassNode& current = walk->get();
            for (auto& method : current.methods) {
                if (method.isConstructor || method.name != node.methodName)
                    continue;
                if (method.isStatic)
                    continue;
                if (method.params.size() != argCount)
                    continue;
                if (method.isPrivate &&
                    !(scope.hasMethod() && &scope.classRef() == &current)) {
                    continue;
                }

                bool match = true;
                for (size_t j = 0; j < argCount; ++j) {
                    const TypeInfo& param = method.paramTypes[j];
                    if (!this->isAssignableTo(param, argTypes[j])) {
                        match = false;
                        break;
                    }
                }

                if (match)
                    candidates.emplace_back(std::ref(current), std::ref(method));
            }

            if (current.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(current.baseClassName);
            if (!baseOpt)
                break;

            walk = baseOpt;
        }

        if (candidates.empty()) {
            diagnostics.addError(node.loc,
                "No matching method '" + node.methodName + "' with " +
                std::to_string(argCount) + " argument(s) in class '" + cls.name + "'");
            return {};
        }

        auto depthOf = [&](const ClassNode& owner) {
            int depth = 0;
            std::optional<std::reference_wrapper<ClassNode>> cur = std::ref(cls);
            for (size_t step = 0; step <= this->classes.size() && cur; ++step) {
                ClassNode& current = cur->get();
                if (&current == &owner)
                    return depth;

                depth++;

                if (current.baseClassName.empty())
                    return std::numeric_limits<int>::max();

                auto baseOpt = this->findClass(current.baseClassName);
                if (!baseOpt)
                    return std::numeric_limits<int>::max();

                cur = baseOpt;
            }

            return std::numeric_limits<int>::max();
        };

        auto best = candidates[0];
        int bestDepth = depthOf(best.first.get());
        size_t bestScore = knownParamCount(best.second.get());

        for (size_t i = 1; i < candidates.size(); ++i) {
            int depth = depthOf(candidates[i].first.get());
            size_t score = knownParamCount(candidates[i].second.get());

            if (depth < bestDepth || (depth == bestDepth && score > bestScore)) {
                best = candidates[i];
                bestDepth = depth;
                bestScore = score;
            }
        }

        MethodDecl& method = best.second.get();

        node.className = cls.name;
        node.methodOrdinal = this->methodOrdinal(cls.name, this->methodSignature(method));

        for (size_t j = 0; j < argCount; ++j) {
            if (method.paramTypes[j].kind == TypeKind::Optional &&
                argTypes[j].kind == TypeKind::Optional) {
                node.args[j]->preserveOptional = true;
            }
        }

        return method.returnType;
    }

    TypeInfo SemanticAnalyzer::checkFuncCall(FuncCallNode& node, MethodScope& scope) {
        size_t argCount = node.args.size();

        std::vector<TypeInfo> argTypes;
        argTypes.reserve(argCount);
        for (auto& arg : node.args)
            argTypes.push_back(checkExpr(*arg, scope));

        auto it = functionsByName.find(node.name);
        if (it == functionsByName.end() || it->second.empty()) {
            if (scope.hasMethod() && scope.methodRef().isStatic && scope.hasClass()) {
                if (auto staticMethod = this->findStaticMethod(
                        scope.classRef(), node.name, argTypes, scope)) {
                    MethodDecl& method = staticMethod->method.get();
                    node.isStaticCall = true;
                    node.staticClassName = scope.classRef().name;
                    node.methodOrdinal = this->staticMethodOrdinal(
                        scope.classRef().name, this->methodSignature(method)
                    );

                    for (size_t j = 0; j < argCount; ++j) {
                        if (method.paramTypes[j].kind == TypeKind::Optional &&
                            argTypes[j].kind == TypeKind::Optional) {
                            node.args[j]->preserveOptional = true;
                        }
                    }

                    return method.returnType;
                }
            }

            if (this->isNameDefined(node.name, scope)) {
                TypeInfo variableType = this->lookupName(node.name, scope);

                if (variableType.kind != TypeKind::Unknown &&
                    variableType.kind != TypeKind::Function) {
                    diagnostics.addError(node.loc,
                        "Value '" + node.name + "' is not callable");
                    return {};
                }

                node.isCallable = true;
                node.resolvedName = node.name;
                return {};
            }

            diagnostics.addError(node.loc,
                "No matching function '" + node.name + "' with " +
                std::to_string(argCount) + " argument(s)");
            return {};
        }

        std::vector<size_t> candidates;
        for (size_t i = 0; i < it->second.size(); ++i) {
            const auto& decl = it->second[i].get().decl;
            if (decl.params.size() != argCount)
                continue;

            bool match = true;
            for (size_t j = 0; j < argCount; ++j) {
                const TypeInfo& param = decl.paramTypes[j];
                if (!this->isAssignableTo(param, argTypes[j])) {
                    match = false;
                    break;
                }
            }

            if (match)
                candidates.push_back(i);
        }

        if (candidates.empty()) {
            diagnostics.addError(node.loc,
                "No matching function '" + node.name + "' with " +
                std::to_string(argCount) + " argument(s)");
            return {};
        }

        size_t best = candidates[0];
        size_t bestScore = knownParamCount(it->second[best].get().decl);
        for (size_t i = 1; i < candidates.size(); ++i) {
            size_t score = knownParamCount(it->second[candidates[i]].get().decl);
            if (score > bestScore) {
                best = candidates[i];
                bestScore = score;
            }
        }

        MethodDecl& decl = it->second[best].get().decl;

        node.resolvedName = node.name;
        node.functionOrdinal = static_cast<int>(best);

        for (size_t j = 0; j < argCount; ++j) {
            if (decl.paramTypes[j].kind == TypeKind::Optional &&
                argTypes[j].kind == TypeKind::Optional) {
                node.args[j]->preserveOptional = true;
            }
        }

        return decl.returnType;
    }

    TypeInfo SemanticAnalyzer::checkNew(NewNode& node, MethodScope& scope) {
        size_t argCount = node.args.size();

        std::vector<TypeInfo> argTypes;
        argTypes.reserve(argCount);
        for (auto& arg : node.args)
            argTypes.push_back(checkExpr(*arg, scope));

        auto clsOpt = findClass(node.className);
        if (!clsOpt) {
            if (isNativeClass(node.className)) {
                std::vector<CallbackTypeArgs> signatures =
                    ClassCall::getInstance().getConstructorSignatures(node.className);

                if (signatures.empty()) {
                    if (argCount != 0) {
                        diagnostics.addError(node.loc,
                            "Native class '" + node.className + "' has no constructor");
                    }

                    return { TypeKind::Object, node.className };
                }

                for (const auto& signature : signatures) {
                    if (matchesNativeSignature(signature, argTypes))
                        return { TypeKind::Object, node.className };
                }

                diagnostics.addError(node.loc,
                    "No matching constructor for native class '" + node.className + "' with " +
                    std::to_string(argCount) + " argument(s)");
                return { TypeKind::Object, node.className };
            }

            diagnostics.addError(node.loc, "Unknown class: " + node.className);
            return {};
        }

        ClassNode& cls = clsOpt->get();
        auto ctorRef = this->findConstructor(cls);

        if (!ctorRef) {
            if (argCount != 0) {
                diagnostics.addError(node.loc,
                    "Class '" + cls.name + "' has no constructor");
            }

            return { TypeKind::Object, cls.name };
        }

        MethodDecl& ctor = ctorRef->method.get();

        if (ctor.params.size() != argCount) {
            diagnostics.addError(node.loc,
                "Constructor of class '" + cls.name + "' expects " +
                std::to_string(ctor.params.size()) + " argument(s), got " +
                std::to_string(argCount));

            return { TypeKind::Object, cls.name };
        }

        for (size_t j = 0; j < argCount; ++j) {
            if (!this->isAssignableTo(ctor.paramTypes[j], argTypes[j])) {
                diagnostics.addError(node.loc,
                    "Type mismatch for constructor parameter '" + ctor.params[j].name +
                    "': expected " + typeToString(ctor.paramTypes[j]) +
                    ", got " + typeToString(argTypes[j]));
            }

            if (ctor.paramTypes[j].kind == TypeKind::Optional &&
                argTypes[j].kind == TypeKind::Optional) {
                node.args[j]->preserveOptional = true;
            }
        }

        return { TypeKind::Object, cls.name };
    }

    TypeInfo SemanticAnalyzer::checkSuperCall(SuperCallNode& node, MethodScope& scope) {
        if (!scope.hasMethod() || !scope.hasClass()) {
            diagnostics.addError(node.loc, "'super(...)' is only available inside class methods");
            return {};
        }

        MethodDecl& method = scope.methodRef();
        if (method.isStatic) {
            diagnostics.addError(node.loc, "'super(...)' is not available inside static methods");
            return {};
        }

        if (!method.isConstructor) {
            diagnostics.addError(node.loc, "'super(...)' is only available inside constructors");
            return {};
        }

        ClassNode& cls = scope.classRef();
        if (cls.baseClassName.empty()) {
            diagnostics.addError(node.loc, "Class '" + cls.name + "' has no base class");
            return {};
        }

        method.hasSuperCall = true;

        size_t argCount = node.args.size();
        std::vector<TypeInfo> argTypes;
        argTypes.reserve(argCount);
        for (auto& arg : node.args)
            argTypes.push_back(checkExpr(*arg, scope));

        auto baseOpt = this->findClass(cls.baseClassName);
        if (!baseOpt) {
            diagnostics.addError(node.loc, "Unknown base class: " + cls.baseClassName);
            return {};
        }

        auto ctorRef = this->findConstructor(baseOpt->get());

        if (!ctorRef) {
            if (argCount != 0) {
                diagnostics.addError(node.loc, "Base class has no constructor");
            }

            node.constructorIndex = -1;
            return {};
        }

        ClassNode& ctorOwner = ctorRef->owner.get();
        MethodDecl& ctor = ctorRef->method.get();
        node.className = ctorOwner.name;
        for (size_t i = 0; i < ctorOwner.methods.size(); ++i) {
            if (&ctorOwner.methods[i] == &ctor) {
                node.constructorIndex = static_cast<int>(i);
                break;
            }
        }

        if (ctor.params.size() != argCount) {
            diagnostics.addError(node.loc,
                "Base constructor of class '" + cls.baseClassName + "' expects " +
                std::to_string(ctor.params.size()) + " argument(s), got " +
                std::to_string(argCount));
            return {};
        }

        for (size_t j = 0; j < argCount; ++j) {
            if (!this->isAssignableTo(ctor.paramTypes[j], argTypes[j])) {
                diagnostics.addError(node.loc,
                    "Type mismatch for base constructor parameter '" + ctor.params[j].name +
                    "': expected " + typeToString(ctor.paramTypes[j]) +
                    ", got " + typeToString(argTypes[j]));
            }

            if (ctor.paramTypes[j].kind == TypeKind::Optional &&
                argTypes[j].kind == TypeKind::Optional) {
                node.args[j]->preserveOptional = true;
            }
        }

        return {};
    }

    TypeInfo SemanticAnalyzer::checkInstanceOf(InstanceOfNode& node, MethodScope& scope) {
        checkExpr(*node.target, scope);

        if (!this->findClass(node.className) && !isNativeClass(node.className)) {
            diagnostics.addError(node.loc, "Unknown class: " + node.className);
        }

        return { TypeKind::Bool };
    }

    TypeInfo SemanticAnalyzer::checkReturn(ReturnNode& node, MethodScope& scope) {
        if (!scope.hasMethod()) {
            diagnostics.addError(node.loc, "'return' is only available inside functions or class methods");
            return {};
        }

        MethodDecl& method = scope.methodRef();
        method.hasReturnStatement = true;

        TypeInfo valueType;
        if (node.value) {
            valueType = checkExpr(*node.value, scope);

            if (method.returnType.kind == TypeKind::Void) {
                diagnostics.addError(node.loc,
                    "Void function '" + method.name + "' cannot return a value");
                return valueType;
            }

            if (method.hasReturnType &&
                method.returnType.kind != TypeKind::Unknown &&
                valueType.kind != TypeKind::Unknown &&
                !this->isAssignableTo(method.returnType, valueType)) {
                diagnostics.addError(node.loc,
                    "Return type mismatch in function '" + method.name +
                    "': expected " + typeToString(method.returnType) +
                    ", got " + typeToString(valueType));
            }

            if (method.returnType.kind == TypeKind::Optional &&
                valueType.kind == TypeKind::Optional) {
                node.value->preserveOptional = true;
            }
        }

        return valueType;
    }

    TypeInfo SemanticAnalyzer::checkLambda(LambdaNode& node, MethodScope& scope) {
        MethodDecl& decl = node.decl;
        decl.paramTypes.resize(decl.params.size());

        for (size_t i = 0; i < decl.params.size(); ++i) {
            if (decl.params[i].hasType)
                decl.paramTypes[i] = this->resolveTypeExpr(
                    decl.params[i].typeExpr, decl.loc, true);
        }

        if (decl.hasReturnType)
            decl.returnType = this->resolveTypeExpr(
                decl.returnTypeExpr, decl.loc, true);

        MethodScope lambdaScope;
        if (scope.hasClass())
            lambdaScope.cls = scope.cls;
        lambdaScope.method = std::ref(decl);

        if (decl.body)
            checkStatement(*decl.body, lambdaScope);

        if (decl.hasReturnType &&
            decl.returnType.kind != TypeKind::Void && !decl.hasReturnStatement) {
            diagnostics.addWarning(decl.loc,
                "Missing return statement in anonymous function");
        }

        return { TypeKind::Function };
    }

    TypeInfo SemanticAnalyzer::lookupName(const std::string& name, MethodScope& scope) {
        if (scope.hasMethod()) {
            MethodDecl& method = scope.methodRef();
            for (size_t i = 0; i < method.params.size(); ++i) {
                if (method.params[i].name == name)
                    return method.paramTypes[i];
            }

            if (scope.hasClass() && !scope.methodRef().isStatic) {
                if (auto field = findField(scope.classRef(), name)) {
                    if (field->member.get().isPrivate && &scope.classRef() != &field->owner.get())
                        return {};

                    return field->member.get().type;
                }
            }

            if (scope.hasClass()) {
                if (auto staticField = findStaticField(scope.classRef(), name)) {
                    if (staticField->member.get().isPrivate && &scope.classRef() != &staticField->owner.get())
                        return {};

                    return staticField->member.get().type;
                }
            }
        }

        if (auto declaredIt = declaredGlobals.find(name);
            declaredIt != declaredGlobals.end()) {
            return declaredIt->second;
        }

        auto it = globalTypes.find(name);
        if (it != globalTypes.end())
            return it->second;

        return {};
    }

    bool SemanticAnalyzer::isNameDefined(const std::string& name, MethodScope& scope) const {
        if (scope.hasMethod()) {
            const MethodDecl& method = scope.methodRef();
            for (const auto& param : method.params) {
                if (param.name == name)
                    return true;
            }

            if (scope.hasClass()) {
                if (!scope.methodRef().isStatic && findField(scope.classRef(), name))
                    return true;

                if (findStaticField(scope.classRef(), name))
                    return true;
            }
        }

        return globalTypes.find(name) != globalTypes.end() ||
               declaredGlobals.find(name) != declaredGlobals.end();
    }

    void SemanticAnalyzer::unify(TypeInfo& target, const TypeInfo& from, SourceLocation loc, const std::string& what) {
        if (from.kind == TypeKind::Unknown || from.kind == TypeKind::Void)
            return;

        if (target.kind == TypeKind::Unknown) {
            target = from;
            return;
        }

        if (from.kind == TypeKind::None && target.kind != TypeKind::Optional) {
            diagnostics.addError(loc,
                "'None' is only allowed when assigning to an optional value (" + what + ")");
            return;
        }

        if (!this->isAssignableTo(target, from)) {
            std::string hint = from.kind == TypeKind::Optional && target.kind != TypeKind::Optional
                ? " (consider using '??' to provide a default value)"
                : "";

            diagnostics.addError(loc,
                "Type mismatch for " + what + ": expected " +
                typeToString(target) + ", got " + typeToString(from) + hint);
        }
    }

    bool SemanticAnalyzer::isAssignableTo(const TypeInfo& target, const TypeInfo& from) const {
        if (target.kind == TypeKind::Unknown ||
            from.kind == TypeKind::Unknown ||
            from.kind == TypeKind::Void) {
            return true;
        }

        if (from.kind == TypeKind::None)
            return target.kind == TypeKind::Optional;

        if (from.kind == TypeKind::Optional) {
            if (target.kind == TypeKind::Optional)
                return this->isAssignableTo(*target.optionalInner, *from.optionalInner);

            if (target.kind == TypeKind::Variant) {
                for (const auto& option : target.variantOptions) {
                    if (this->isAssignableTo(option, from))
                        return true;
                }
            }

            return false;
        }

        if (target.kind == TypeKind::None)
            return false;

        if (target.kind == TypeKind::Optional)
            return this->isAssignableTo(*target.optionalInner, from);

        if (target.kind == TypeKind::Variant) {
            return std::ranges::any_of(target.variantOptions, [this, from](const TypeInfo& option) -> bool {
                return this->isAssignableTo(option, from);
            });
        }

        return this->isTypeCompatible(target, from);
    }

    size_t SemanticAnalyzer::knownParamCount(const MethodDecl& method) const {
        size_t count = 0;
        for (const auto& type : method.paramTypes) {
            if (type.kind != TypeKind::Unknown)
                count++;
        }
        return count;
    }

    std::string SemanticAnalyzer::methodSignature(const MethodDecl& method) const {
        std::string signature = method.name + "(";
        for (size_t i = 0; i < method.paramTypes.size(); ++i) {
            if (i != 0)
                signature += ",";

            signature += this->typeToString(method.paramTypes[i]);
        }
        signature += ")";
        return signature;
    }

    int SemanticAnalyzer::methodOrdinal(const std::string& className, const std::string& signature) const {
        auto classIt = this->classMethodOrdinals.find(className);
        if (classIt == this->classMethodOrdinals.end())
            return -1;

        auto sigIt = classIt->second.find(signature);
        if (sigIt == classIt->second.end())
            return -1;

        return sigIt->second;
    }

    bool SemanticAnalyzer::isDerived(const std::string& derivedName, const std::string& baseName) const {
        if (derivedName == baseName)
            return true;

        std::string current = derivedName;
        for (size_t step = 0; step <= this->classes.size() && !current.empty(); ++step) {
            auto clsOpt = this->findClass(current);
            if (!clsOpt)
                return false;

            ClassNode& cls = clsOpt->get();
            if (cls.baseClassName == baseName)
                return true;

            current = cls.baseClassName;
        }

        return false;
    }

    bool SemanticAnalyzer::isTypeCompatible(const TypeInfo& target, const TypeInfo& from) const {
        if (target == from)
            return true;

        if (target.kind == TypeKind::Object && from.kind == TypeKind::Object)
            return this->isDerived(from.className, target.className);

        return false;
    }

    std::optional<SemanticAnalyzer::FieldRef> SemanticAnalyzer::findField(ClassNode& cls, const std::string& name) const {
        std::optional<std::reference_wrapper<ClassNode>> current = std::ref(cls);
        for (size_t step = 0; step <= this->classes.size() && current; ++step) {
            ClassNode& clsRef = current->get();
            for (auto& member : clsRef.members) {
                if (!member.isStatic && member.name == name) {
                    return FieldRef{ std::ref(member), std::ref(clsRef) };
                }
            }

            if (clsRef.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(clsRef.baseClassName);
            if (!baseOpt)
                break;

            current = baseOpt;
        }

        return std::nullopt;
    }

    std::optional<SemanticAnalyzer::ConstructorRef> SemanticAnalyzer::findConstructor(ClassNode& cls) const {
        std::optional<std::reference_wrapper<ClassNode>> current = std::ref(cls);
        for (size_t step = 0; step <= this->classes.size() && current; ++step) {
            ClassNode& clsRef = current->get();
            for (auto& method : clsRef.methods) {
                if (method.isConstructor) {
                    return ConstructorRef{ std::ref(method), std::ref(clsRef) };
                }
            }

            if (clsRef.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(clsRef.baseClassName);
            if (!baseOpt)
                break;

            current = baseOpt;
        }

        return std::nullopt;
    }

    std::optional<SemanticAnalyzer::FieldRef> SemanticAnalyzer::findStaticField(ClassNode& cls, const std::string& name) const {
        std::optional<std::reference_wrapper<ClassNode>> current = std::ref(cls);
        for (size_t step = 0; step <= this->classes.size() && current; ++step) {
            ClassNode& clsRef = current->get();
            for (auto& member : clsRef.members) {
                if (member.isStatic && member.name == name) {
                    return FieldRef{ std::ref(member), std::ref(clsRef) };
                }
            }

            if (clsRef.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(clsRef.baseClassName);
            if (!baseOpt)
                break;

            current = baseOpt;
        }

        return std::nullopt;
    }

    std::optional<SemanticAnalyzer::StaticMethodRef> SemanticAnalyzer::findStaticMethod(
        ClassNode& cls, const std::string& name, const std::vector<TypeInfo>& argTypes,
        const MethodScope& scope
    ) const {
        std::vector<std::pair<std::reference_wrapper<ClassNode>, std::reference_wrapper<MethodDecl>>> candidates;
        std::optional<std::reference_wrapper<ClassNode>> walk = std::ref(cls);

        for (size_t step = 0; step <= this->classes.size() && walk; ++step) {
            ClassNode& current = walk->get();
            for (auto& method : current.methods) {
                if (method.isConstructor || !method.isStatic || method.name != name)
                    continue;
                if (method.params.size() != argTypes.size())
                    continue;
                if (method.isPrivate &&
                    !(scope.hasClass() && scope.hasMethod() && &scope.classRef() == &current)) {
                    continue;
                }

                bool match = true;
                for (size_t j = 0; j < argTypes.size(); ++j) {
                    const TypeInfo& param = method.paramTypes[j];
                    if (!this->isAssignableTo(param, argTypes[j])) {
                        match = false;
                        break;
                    }
                }

                if (match)
                    candidates.emplace_back(std::ref(current), std::ref(method));
            }

            if (current.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(current.baseClassName);
            if (!baseOpt)
                break;

            walk = baseOpt;
        }

        if (candidates.empty())
            return std::nullopt;

        auto depthOf = [&](const ClassNode& owner) {
            int depth = 0;
            std::optional<std::reference_wrapper<ClassNode>> cur = std::ref(cls);
            for (size_t step = 0; step <= this->classes.size() && cur; ++step) {
                ClassNode& current = cur->get();
                if (&current == &owner)
                    return depth;

                depth++;

                if (current.baseClassName.empty())
                    return std::numeric_limits<int>::max();

                auto baseOpt = this->findClass(current.baseClassName);
                if (!baseOpt)
                    return std::numeric_limits<int>::max();

                cur = baseOpt;
            }

            return std::numeric_limits<int>::max();
        };

        auto best = candidates[0];
        int bestDepth = depthOf(best.first.get());
        size_t bestScore = knownParamCount(best.second.get());

        for (size_t i = 1; i < candidates.size(); ++i) {
            int depth = depthOf(candidates[i].first.get());
            size_t score = knownParamCount(candidates[i].second.get());

            if (depth < bestDepth || (depth == bestDepth && score > bestScore)) {
                best = candidates[i];
                bestDepth = depth;
                bestScore = score;
            }
        }

        return StaticMethodRef{ best.second, best.first };
    }

    int SemanticAnalyzer::staticMethodOrdinal(const std::string& className, const std::string& signature) const {
        auto classIt = this->classStaticMethodOrdinals.find(className);
        if (classIt == this->classStaticMethodOrdinals.end())
            return -1;

        auto sigIt = classIt->second.find(signature);
        if (sigIt == classIt->second.end())
            return -1;

        return sigIt->second;
    }
}
