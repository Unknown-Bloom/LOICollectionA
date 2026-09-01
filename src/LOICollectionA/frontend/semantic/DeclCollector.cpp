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

#include "LOICollectionA/frontend/semantic/Helpers.h"

namespace LOICollection::frontend {

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

    void SemanticAnalyzer::registerImpl(ImplNode& node) {
        this->impls.push_back(std::ref(node));
    }

    void SemanticAnalyzer::processImpls() {
        for (auto implRef : this->impls) {
            ImplNode& impl = implRef.get();

            const std::string targetName = impl.target.name;
            auto clsOpt = this->findClass(targetName);
            if (!clsOpt) {
                this->diagnostics.addError(impl.loc,
                    "impl target '" + targetName + "' is not a class");
                continue;
            }
            ClassNode& cls = clsOpt->get();

            std::string traitName;
            if (impl.trait) {
                traitName = impl.trait->name;
                if (!this->traits.contains(traitName)) {
                    this->diagnostics.addError(impl.loc,
                        "Cannot implement unknown trait '" + traitName + "'");
                    continue;
                }
            }

            std::unordered_set<std::string> classMethodNames;
            for (const auto& m : cls.methods)
                classMethodNames.insert(m.name);
            std::unordered_set<std::string> constNames;
            for (const auto& c : cls.members)
                constNames.insert(c.name);

            for (auto& method : impl.methods) {
                bool duplicate = false;
                for (const auto& existing : cls.methods) {
                    if (existing.name == method.name &&
                        existing.params.size() == method.params.size()) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) {
                    this->diagnostics.addError(method.loc,
                        "Method '" + method.name + "' is already defined in class '" +
                        targetName + "'");
                    continue;
                }
                cls.methods.push_back(std::move(method));
            }

            for (auto& member : impl.consts) {
                if (constNames.contains(member.name)) {
                    this->diagnostics.addError(member.loc,
                        "Duplicate associated constant '" + member.name +
                        "' in impl for '" + targetName + "'");
                    continue;
                }
                if (!member.hasTypeExpr && member.hasDefault && member.defaultExpr &&
                    member.defaultExpr->getType() == ASTNode::Type::Value) {
                    auto& literal = static_cast<ValueNode&>(*member.defaultExpr);
                    member.type = this->typeOfValue(literal.value);
                }
                cls.members.push_back(std::move(member));
                constNames.insert(member.name);
            }

            if (impl.trait) {
                const auto& required = this->traits[traitName];
                for (const auto& req : required) {
                    bool satisfied = false;
                    for (const auto& m : cls.methods) {
                        if (m.name == req.name && m.params.size() == req.paramCount) {
                            satisfied = true;
                            break;
                        }
                    }
                    if (!satisfied)
                        this->diagnostics.addError(impl.loc,
                            "impl of trait '" + traitName + "' for '" + targetName +
                            "' is missing required method '" + req.name + "'");
                }
            }
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

}
