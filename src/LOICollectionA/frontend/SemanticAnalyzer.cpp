#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "LOICollectionA/base/ScopeGuard.h"

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Iteration.h"

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

    TypeInfo arithmeticResult(const std::string& op, const TypeInfo& left, const TypeInfo& right) {
        auto numeric = [](const TypeInfo& t) { return t.kind == TypeKind::Int || t.kind == TypeKind::Float; };

        if (op == "+" && (left.kind == TypeKind::String || right.kind == TypeKind::String))
            return { TypeKind::String };

        if (!(numeric(left) && numeric(right)))
            return {};

        bool bothInt = left.kind == TypeKind::Int && right.kind == TypeKind::Int;
        if (bothInt && (op == "+" || op == "-" || op == "*" || op == "%"))
            return { TypeKind::Int };

        return { TypeKind::Float };
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
        this->traits.clear();
        this->activeTypeParams.clear();
        this->activeTypeParamBounds.clear();
        this->blockScopes.clear();
        this->formReceivers.clear();
        this->receiverBindings.clear();
        this->receiverCaptures.clear();
        this->closureDepth = 0;

        this->collectTypeAliases(root);

        for (auto& part : root.parts) {
            switch (part->getType()) {
                case ASTNode::Type::Class:
                    registerClass(static_cast<ClassNode&>(*part));
                    break;
                case ASTNode::Type::FunctionDef:
                    registerFunction(static_cast<FunctionDefNode&>(*part));
                    break;
                case ASTNode::Type::Trait:
                    registerTrait(static_cast<TraitNode&>(*part));
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

    ClassLookup SemanticAnalyzer::classLookup() const {
        return [this](const std::string& name) -> ClassNode* {
            auto cls = this->findClass(name);

            return cls ? &cls->get() : nullptr;
        };
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

    namespace {
        const std::unordered_map<std::string, TypeKind>& basicTypes() {
            static const std::unordered_map<std::string, TypeKind> table = {
                {"int", TypeKind::Int},
                {"float", TypeKind::Float},
                {"string", TypeKind::String},
                {"bool", TypeKind::Bool},
                {"void", TypeKind::Void},
            };
            return table;
        }
    }

    TypeInfo SemanticAnalyzer::typeFromName(const std::string& name, SourceLocation loc, bool reportError) const {
        if (auto it = this->activeTypeParams.find(name); it != this->activeTypeParams.end())
            return it->second;

        if (auto it = basicTypes().find(name); it != basicTypes().end())
            return { it->second };

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

    void SemanticAnalyzer::registerTrait(TraitNode& node) {
        if (this->traits.contains(node.name)) {
            this->diagnostics.addError(node.loc, "Duplicate trait: " + node.name);
            return;
        }

        std::vector<TraitMethod> methods;
        for (const auto& m : node.methods) {
            TraitMethod tm;
            tm.name = m.name;
            tm.paramCount = m.params.size();
            tm.hasReturnType = m.hasReturnType;
            tm.returnTypeExpr = m.returnTypeExpr;
            methods.push_back(std::move(tm));
        }

        this->traits[node.name] = std::move(methods);
    }

    namespace {
        bool isReservedTypeName(const std::string& name) {
            return basicTypes().contains(name) || name == "variant" || name == "optional";
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

    TypeInfo SemanticAnalyzer::substituteType(
        const TypeInfo& type, const std::unordered_map<std::string, TypeInfo>& subst) const {
        if (type.kind == TypeKind::Generic) {
            auto it = subst.find(type.typeVar);
            if (it != subst.end() && it->second.kind != TypeKind::Unknown &&
                it->second.kind != TypeKind::None)
                return it->second;
            return type;
        }

        TypeInfo result = type;
        if (type.optionalInner)
            result.optionalInner = std::make_shared<TypeInfo>(
                substituteType(*type.optionalInner, subst));
        for (size_t i = 0; i < result.variantOptions.size(); ++i)
            result.variantOptions[i] = substituteType(result.variantOptions[i], subst);
        return result;
    }

    std::unordered_map<std::string, TypeInfo> SemanticAnalyzer::inferTypeParams(
        const MethodDecl& decl, const std::vector<TypeInfo>& argTypes) const {
        std::unordered_map<std::string, TypeInfo> subst;
        for (const auto& tp : decl.typeParams)
            subst[tp.name] = {};

        for (size_t j = 0; j < argTypes.size() && j < decl.paramTypes.size(); ++j) {
            const TypeInfo& param = decl.paramTypes[j];
            if (param.kind != TypeKind::Generic)
                continue;

            auto it = subst.find(param.typeVar);
            if (it == subst.end())
                continue;

            if (it->second.kind == TypeKind::Unknown)
                it->second = argTypes[j];
            else if (it->second != argTypes[j])
                it->second = {};
        }

        return subst;
    }

    bool SemanticAnalyzer::satisfiesTrait(const TypeInfo& type, const std::string& traitName) const {
        auto traitIt = this->traits.find(traitName);
        if (traitIt == this->traits.end())
            return false;

        if (type.kind != TypeKind::Object)
            return false;

        auto clsOpt = this->findClass(type.className);
        if (!clsOpt)
            return false;

        ClassNode& cls = clsOpt->get();
        for (const auto& reqMethod : traitIt->second) {
            bool found = false;
            for (const auto& m : cls.methods) {
                if (m.name != reqMethod.name || m.params.size() != reqMethod.paramCount)
                    continue;
                found = true;
                break;
            }
            if (!found)
                return false;
        }

        return true;
    }

    bool SemanticAnalyzer::boundsSatisfied(
        const std::vector<TypeParam>& typeParams,
        const std::unordered_map<std::string, TypeInfo>& subst) const {
        for (const auto& tp : typeParams) {
            if (tp.bounds.empty())
                continue;

            auto it = subst.find(tp.name);
            if (it == subst.end() || it->second.kind == TypeKind::Unknown)
                continue;

            for (const auto& bound : tp.bounds) {
                if (!this->satisfiesTrait(it->second, bound))
                    return false;
            }
        }

        return true;
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
                for (const auto& tp : method.typeParams)
                    for (const auto& bound : tp.bounds)
                        if (!this->traits.contains(bound))
                            this->diagnostics.addError(method.loc,
                                "Unknown trait bound '" + bound + "' for type parameter '" + tp.name + "'");

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

            for (const auto& tp : decl.typeParams)
                for (const auto& bound : tp.bounds)
                    if (!this->traits.contains(bound))
                        this->diagnostics.addError(decl.loc,
                            "Unknown trait bound '" + bound + "' for type parameter '" + tp.name + "'");

            this->activeTypeParams.clear();
            for (const auto& tp : decl.typeParams) {
                TypeInfo g;
                g.kind = TypeKind::Generic;
                g.typeVar = tp.name;
                this->activeTypeParams[tp.name] = g;
            }

            decl.paramTypes.resize(decl.params.size());
            for (size_t i = 0; i < decl.params.size(); ++i) {
                if (decl.params[i].hasType)
                    decl.paramTypes[i] = this->resolveTypeExpr(
                        decl.params[i].typeExpr, decl.loc, true);
            }

            if (decl.hasReturnType)
                decl.returnType = this->resolveTypeExpr(decl.returnTypeExpr, decl.loc, true);

            this->activeTypeParams.clear();
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
                case ASTNode::Type::Trait:
                case ASTNode::Type::Using:
                case ASTNode::Type::Import:
                case ASTNode::Type::Component:
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

        bool pushedTypeParams = false;
        if (!method.typeParams.empty()) {
            pushedTypeParams = true;
            this->activeTypeParams.clear();
            this->activeTypeParamBounds.clear();
            for (const auto& tp : method.typeParams) {
                TypeInfo g;
                g.kind = TypeKind::Generic;
                g.typeVar = tp.name;
                this->activeTypeParams[tp.name] = g;
                this->activeTypeParamBounds[tp.name] = tp.bounds;
            }
        }

        if (method.body) {
            this->blockScopes.emplace_back();
            auto popScope = make_scope_guard([this] { this->blockScopes.pop_back(); });

            checkStatement(*method.body, scope);
        }

        if (pushedTypeParams) {
            this->activeTypeParams.clear();
            this->activeTypeParamBounds.clear();
        }

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
            case ASTNode::Type::Import:
                return;
            case ASTNode::Type::Return:
                checkReturn(static_cast<ReturnNode&>(node), scope);
                return;
            case ASTNode::Type::While: {
                auto& whileNode = static_cast<WhileNode&>(node);
                ++scope.loopDepth;
                auto loops = make_scope_guard([&scope] { --scope.loopDepth; });

                if (whileNode.condition)
                    checkExpr(*whileNode.condition, scope);
                if (whileNode.body)
                    checkStatement(*whileNode.body, scope);
                return;
            }
            case ASTNode::Type::For: {
                auto& forNode = static_cast<ForNode&>(node);
                ++scope.loopDepth;
                auto loops = make_scope_guard([&scope] { --scope.loopDepth; });

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

                ++scope.loopDepth;
                auto loops = make_scope_guard([&scope] { --scope.loopDepth; });

                TypeInfo iterableType = checkExpr(*forIn.iterable, scope);

                auto protocol = iterableProtocol(forIn.iterable->getType(), iterableType, this->classLookup());
                if (!protocol) {
                    this->diagnostics.addError(forIn.loc,
                        "for-in iterable must be an array, a string or a class providing '" +
                        std::string(lengthMethod) + "()' and '" + std::string(elementMethod) +
                        "(int)', got " + typeInfoToString(iterableType));
                }

                auto declareLoopVar = [this](const std::string& name, TypeInfo type) {
                    if (this->blockScopes.empty())
                        this->globalTypes[name] = std::move(type);
                    else
                        this->blockScopes.back()[name] = std::move(type);
                };

                if (forIn.hasIndexVar)
                    declareLoopVar(forIn.indexVar, TypeInfo{ TypeKind::Int });
                declareLoopVar(forIn.elementVar,
                    protocol ? protocol->elementType : TypeInfo{});

                if (forIn.body)
                    checkStatement(*forIn.body, scope);
                return;
            }
            case ASTNode::Type::Break:
                if (scope.loopDepth == 0)
                    this->diagnostics.addError(static_cast<BreakNode&>(node).loc,
                        "'break' can only be used inside a loop");
                return;
            case ASTNode::Type::Continue:
                if (scope.loopDepth == 0)
                    this->diagnostics.addError(static_cast<ContinueNode&>(node).loc,
                        "'continue' can only be used inside a loop");
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
                return arithmeticResult(assign.op, target, value);
            }

            case ASTNode::Type::Coalesce: {
                auto& coalesce = static_cast<CoalesceNode&>(node);
                TypeInfo left = checkExpr(*coalesce.left, scope);
                TypeInfo right = checkExpr(*coalesce.right, scope);

                
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

                if (this->closureDepth > 0) {
                    for (const auto& binding : this->receiverBindings) {
                        if (binding.name != var.name)
                            continue;

                        this->receiverCaptures.push_back({ var.name, var.loc });
                        break;
                    }
                }

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
                if (scope.hasMethod() && scope.hasClass()) {
                    if (scope.methodRef().isStatic) {
                        diagnostics.addError(self.loc, "'this' is not available inside static methods");
                        return {};
                    }

                    return { TypeKind::Object, scope.classRef().name };
                }

                if (!this->formReceivers.empty())
                    return { TypeKind::Object, this->formReceivers.back() };

                if (this->closureDepth > 0 && !this->receiverBindings.empty())
                    return { TypeKind::Object, this->receiverBindings.back().className };

                diagnostics.addError(self.loc, "'this' is only available inside class methods");
                return {};
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
                return arithmeticResult(arith.op, left, right);
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

                if (node.isDeclaration && !this->blockScopes.empty()) {
                    auto& locals = this->blockScopes.back();
                    if (locals.contains(var.name)) {
                        this->diagnostics.addError(node.loc,
                            "Variable '" + var.name + "' is already declared in this scope");
                        return rhs;
                    }

                    if (node.hasDeclaredType) {
                        if (!node.value) {
                            this->diagnostics.addError(node.loc,
                                "Typed declaration of '" + var.name + "' requires an initializer");
                            return rhs;
                        }

                        TypeInfo declared = this->resolveTypeExpr(node.declaredType, node.loc, true);
                        this->unify(declared, rhs, node.loc, "variable '" + var.name + "'");
                        if (declared.kind == TypeKind::Optional &&
                            rhs.kind == TypeKind::Optional && node.value) {
                            node.value->preserveOptional = true;
                        }
                        locals[var.name] = declared;
                        return rhs;
                    }

                    locals[var.name] = rhs;
                    return rhs;
                }

                if (node.isDeclaration &&
                    (this->declaredGlobals.contains(var.name) ||
                     this->globalTypes.contains(var.name))) {
                    this->diagnostics.addError(node.loc,
                        "Variable '" + var.name + "' is already declared in this scope");
                    return rhs;
                }

                if (!this->blockScopes.empty()) {
                    for (auto it = this->blockScopes.rbegin(); it != this->blockScopes.rend(); ++it) {
                        auto found = it->find(var.name);
                        if (found != it->end()) {
                            if (rhs.kind != TypeKind::Unknown && rhs.kind != TypeKind::None)
                                found->second = rhs;
                            return rhs;
                        }
                    }

                    if (!this->isNameDefined(var.name, scope)) {
                        this->requireLet(node, var);
                        this->blockScopes.back()[var.name] = rhs;
                        return rhs;
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
                        this->requireLet(node, var);
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

                if (this->globalTypes.contains(var.name)) {
                    auto& existing = this->globalTypes[var.name];
                    this->unify(existing, rhs, node.loc, "variable '" + var.name + "'");
                    if (existing.kind == TypeKind::Optional &&
                        rhs.kind == TypeKind::Optional && node.value) {
                        node.value->preserveOptional = true;
                    }
                    return rhs;
                }

                if (this->declaredGlobals.contains(var.name)) {
                    auto& existing = this->declaredGlobals[var.name];
                    this->unify(existing, rhs, node.loc, "variable '" + var.name + "'");
                    if (existing.kind == TypeKind::Optional &&
                        rhs.kind == TypeKind::Optional && node.value) {
                        node.value->preserveOptional = true;
                    }
                    return rhs;
                }

                if (rhs.kind == TypeKind::None) {
                    this->requireLet(node, var);
                    globalTypes[var.name] = TypeInfo{};
                    return rhs;
                }

                this->requireLet(node, var);

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
            static const std::unordered_map<std::string, MemberAccessNode::MemberKind> conventionMembers = {
                { "type", MemberAccessNode::MemberKind::TypeOf },
                { "value", MemberAccessNode::MemberKind::Value },
                { "has_value", MemberAccessNode::MemberKind::HasValue },
            };

            auto it = conventionMembers.find(node.memberName);
            if (it != conventionMembers.end()) {
                node.target->preserveOptional = true;

                switch (it->second) {
                    case MemberAccessNode::MemberKind::TypeOf:
                        node.memberKind = it->second;
                        return { TypeKind::String };

                    case MemberAccessNode::MemberKind::Value: {
                        node.memberKind = it->second;
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

                    case MemberAccessNode::MemberKind::HasValue:
                        if (targetType.kind != TypeKind::Optional) {
                            this->diagnostics.addError(node.loc,
                                "'.has_value' is only available on optional values");
                            return {};
                        }
                        node.memberKind = it->second;
                        return { TypeKind::Bool };

                    default: break;
                }
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
            diagnostics.addError(node.loc, "Array has no member '" + node.memberName + "'");
            return {};
        }

        if (targetType.kind == TypeKind::String) {
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

    std::string SemanticAnalyzer::receiverVariable(MethodCallNode& node) const {
        if (node.isStaticCall || node.target->getType() != ASTNode::Type::Variable)
            return {};

        const std::string& name = static_cast<VariableNode&>(*node.target).name;
        if (this->classByName.contains(name) || isNativeClass(name))
            return {};

        return name;
    }

    void SemanticAnalyzer::reportReceiverCaptures(const std::string& name) {
        std::vector<ReceiverCapture> remaining;
        for (auto& capture : this->receiverCaptures) {
            if (capture.name != name) {
                remaining.push_back(capture);
                continue;
            }

            this->diagnostics.addError(capture.loc,
                "Callback captures its receiver '" + name +
                "'; the closure would retain the object and leak it, use 'this' instead");
        }

        this->receiverCaptures = std::move(remaining);
    }

    void SemanticAnalyzer::requireLet(AssignmentNode& node, const VariableNode& var) {
        if (node.isDeclaration)
            return;

        this->diagnostics.addError(node.loc,
            "Variable '" + var.name + "' is not declared; write 'let " + var.name + " = ...' to introduce it");
    }

    TypeInfo SemanticAnalyzer::checkMethodCall(MethodCallNode& node, MethodScope& scope) {
        size_t argCount = node.args.size();

        auto checkArgs = [&] {
            std::vector<TypeInfo> types;
            types.reserve(argCount);
            for (auto& arg : node.args)
                types.push_back(checkExpr(*arg, scope));
            return types;
        };

        if (node.target->getType() == ASTNode::Type::Variable) {
            auto& var = static_cast<VariableNode&>(*node.target);
            if (auto clsOpt = this->findClass(var.name)) {
                std::vector<TypeInfo> argTypes = checkArgs();

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
                std::vector<TypeInfo> argTypes = checkArgs();

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

        std::string receiverVar;
        if (targetType.kind == TypeKind::Object)
            receiverVar = this->receiverVariable(node);

        std::vector<TypeInfo> argTypes;
        if (!receiverVar.empty()) {
            this->receiverBindings.push_back({ receiverVar, targetType.className });
            auto binding = make_scope_guard([this] { this->receiverBindings.pop_back(); });

            argTypes = checkArgs();
            this->reportReceiverCaptures(receiverVar);
        } else {
            argTypes = checkArgs();
        }

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

        if (targetType.kind == TypeKind::Generic) {
            auto boundIt = this->activeTypeParamBounds.find(targetType.typeVar);
            if (boundIt != this->activeTypeParamBounds.end()) {
                for (const auto& traitName : boundIt->second) {
                    auto traitIt = this->traits.find(traitName);
                    if (traitIt == this->traits.end())
                        continue;

                    for (const auto& m : traitIt->second) {
                        if (m.name != node.methodName || m.paramCount != argCount)
                            continue;

                        TypeInfo ret;
                        if (m.hasReturnType)
                            ret = this->resolveTypeExpr(m.returnTypeExpr, node.loc, false);
                        node.dynamicDispatch = true;
                        return ret;
                    }
                }
            }

            diagnostics.addError(node.loc,
                "Type '" + targetType.typeVar + "' has no method '" + node.methodName + "'");
            return {};
        }

        if (targetType.kind == TypeKind::Unknown) {
            ClassCall& classes = ClassCall::getInstance();
            for (const auto& className : classes.getClassNames()) {
                std::vector<CallbackTypeArgs> signatures =
                    classes.getValueMethodSignatures(className, node.methodName);

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

        if (node.isFormReceiverCall && !this->formReceivers.empty()) {
            const std::string& receiverClass = this->formReceivers.back();
            std::vector<CallbackTypeArgs> signatures =
                ClassCall::getInstance().getMethodSignatures(receiverClass, node.name);

            for (const auto& signature : signatures) {
                if (matchesNativeSignature(signature, argTypes)) {
                    node.receiverClassName = receiverClass;
                    return {};
                }
            }

            node.isFormReceiverCall = false;
        }

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

        struct Cand {
            size_t index;
            std::unordered_map<std::string, TypeInfo> subst;
        };
        std::vector<Cand> candidates;
        std::vector<std::string> boundFailures;

        for (size_t i = 0; i < it->second.size(); ++i) {
            const auto& decl = it->second[i].get().decl;
            if (decl.params.size() != argCount)
                continue;

            bool match = true;
            std::unordered_map<std::string, TypeInfo> subst;
            if (!decl.typeParams.empty()) {
                subst = this->inferTypeParams(decl, argTypes);
                for (size_t j = 0; j < argCount; ++j) {
                    if (!this->isAssignableTo(
                            this->substituteType(decl.paramTypes[j], subst), argTypes[j])) {
                        match = false;
                        break;
                    }
                }
                if (match && !this->boundsSatisfied(decl.typeParams, subst))
                    match = false;
            } else {
                for (size_t j = 0; j < argCount; ++j) {
                    if (!this->isAssignableTo(decl.paramTypes[j], argTypes[j])) {
                        match = false;
                        break;
                    }
                }
            }

            if (!match)
                continue;

            if (decl.typeParams.empty()) {
                candidates.push_back({ i, {} });
            } else if (this->boundsSatisfied(decl.typeParams, subst)) {
                candidates.push_back({ i, subst });
            } else {
                boundFailures.push_back(decl.name);
            }
        }

        if (candidates.empty()) {
            if (!boundFailures.empty()) {
                diagnostics.addError(node.loc,
                    "Type bound not satisfied for generic function '" + node.name + "'");
            } else {
                diagnostics.addError(node.loc,
                    "No matching function '" + node.name + "' with " +
                    std::to_string(argCount) + " argument(s)");
            }
            return {};
        }

        size_t best = 0;
        size_t bestScore = knownParamCount(it->second[candidates[0].index].get().decl);
        for (size_t i = 1; i < candidates.size(); ++i) {
            size_t score = knownParamCount(it->second[candidates[i].index].get().decl);
            if (score > bestScore) {
                best = i;
                bestScore = score;
            }
        }

        size_t bestIndex = candidates[best].index;
        MethodDecl& decl = it->second[bestIndex].get().decl;

        node.resolvedName = node.name;
        node.functionOrdinal = static_cast<int>(bestIndex);

        for (size_t j = 0; j < argCount; ++j) {
            if (decl.paramTypes[j].kind == TypeKind::Optional &&
                argTypes[j].kind == TypeKind::Optional) {
                node.args[j]->preserveOptional = true;
            }
        }

        return this->substituteType(decl.returnType, candidates[best].subst);
    }

    namespace {
        bool isWhitelistedFormClass(const std::string& name) {
            return name == "CustomForm" || name == "MessageBox" ||
                   name == "PaginatedForm" || name == "ScriptForm";
        }
    }

    void SemanticAnalyzer::checkDeclarativeBlock(NewNode& node, MethodScope& scope) {
        if (!isWhitelistedFormClass(node.className) || !isNativeClass(node.className)) {
            diagnostics.addError(node.loc,
                "Declarative UI blocks are only allowed on form classes "
                "(CustomForm, MessageBox, PaginatedForm, ScriptForm), got '" + node.className + "'");
            return;
        }

        this->blockScopes.emplace_back();
        this->formReceivers.push_back(node.className);

        if (!node.receiverName.empty())
            this->blockScopes.back()[node.receiverName] = { TypeKind::Object, node.className };

        for (auto& part : node.declarativeBlock->parts)
            checkStatement(*part, scope);

        this->formReceivers.pop_back();
        this->blockScopes.pop_back();
    }

    TypeInfo SemanticAnalyzer::checkNew(NewNode& node, MethodScope& scope) {
        size_t argCount = node.args.size();

        std::vector<TypeInfo> argTypes;
        argTypes.reserve(argCount);
        for (auto& arg : node.args)
            argTypes.push_back(checkExpr(*arg, scope));

        if (node.declarativeBlock)
            this->checkDeclarativeBlock(node, scope);

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

        ++this->closureDepth;
        auto closure = make_scope_guard([this] { --this->closureDepth; });

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
        for (auto it = this->blockScopes.rbegin(); it != this->blockScopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end())
                return found->second;
        }

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
        for (const auto& blockScope : this->blockScopes) {
            if (blockScope.contains(name))
                return true;
        }

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
                ? " (consider using '\?\?' to provide a default value)"
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
