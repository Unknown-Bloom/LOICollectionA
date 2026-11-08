#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/ir/hir/HirBuilder.h"

namespace LOICollection::frontend::ir::hir::detail {
    ty::Ty lowerTypeExpr(ty::TypeContext& types, const TypeExpr& expr) {
        if (expr.name == "int")
            return types.primitive(ty::TyKind::Int);
        if (expr.name == "float")
            return types.primitive(ty::TyKind::Float);
        if (expr.name == "string")
            return types.primitive(ty::TyKind::String);
        if (expr.name == "bool")
            return types.primitive(ty::TyKind::Bool);
        if (expr.name == "void")
            return types.primitive(ty::TyKind::Void);
        if (expr.name == "None")
            return types.primitive(ty::TyKind::None);

        if (expr.name == "ref" && !expr.args.empty()) {
            ty::TyData data;
            data.kind = types.kind(lowerTypeExpr(types, expr.args[0]));
            data.ownership = Ownership::Borrowed;
            return types.intern(std::move(data));
        }

        if (expr.name == "optional" && !expr.args.empty())
            return types.optional(lowerTypeExpr(types, expr.args[0]));

        if (expr.name == "variant") {
            std::vector<ty::Ty> options;
            options.reserve(expr.args.size());
            for (const TypeExpr& arg : expr.args)
                options.push_back(lowerTypeExpr(types, arg));
            return types.variant(std::move(options));
        }

        std::vector<ty::Ty> args;
        args.reserve(expr.args.size());
        for (const TypeExpr& arg : expr.args)
            args.push_back(lowerTypeExpr(types, arg));

        return types.adt(expr.name, std::move(args));
    }

    ty::Ty literalType(ty::TypeContext& types, const ValueNode::ValueType& value) {
        switch (value.index()) {
            case 0:
                return types.primitive(ty::TyKind::Int);
            case 1:
                return types.primitive(ty::TyKind::Float);
            case 2:
                return types.primitive(ty::TyKind::String);
            case 3:
                return types.primitive(ty::TyKind::Bool);
            case 7:
                return types.primitive(ty::TyKind::None);
            default:
                return types.dynamic();
        }
    }
}

namespace LOICollection::frontend::ir::hir {
    using namespace detail;

    HirBuilder::HirBuilder(DiagnosticEngine& diag) : diagnostics(diag) {}

    std::string HirBuilder::qualifiedName(const VariableNode& node) const {
        return node.isStaticField ? node.staticClassName + "::" + node.name : node.name;
    }

    std::string HirBuilder::qualifiedName(const MemberAccessNode& node) const {
        return node.isStaticAccess ? node.staticClassName + "::" + node.memberName : node.memberName;
    }

    void HirBuilder::pushScope(bool hasThis, bool inherits) {
        Scope scope;
        scope.body = this->current;
        scope.inherits = inherits;
        scope.hasThis = hasThis || (!this->scopes.empty() && this->scopes.back().hasThis);
        this->scopes.push_back(std::move(scope));
    }

    void HirBuilder::popScope() {
        this->scopes.pop_back();
    }

    LocalId HirBuilder::declareLocal(const std::string& name, ty::Ty type, Ownership ownership, bool parameter) {
        Scope& scope = this->scopes.back();
        if (auto it = scope.locals.find(name); it != scope.locals.end())
            return it->second;

        Local local;
        local.name = name;
        local.type = type.valid() ? type : this->program.types->dynamic();
        local.ownership = ownership;
        local.parameter = parameter;

        this->current->locals.push_back(std::move(local));

        const LocalId id = static_cast<LocalId>(this->current->locals.size() - 1);
        scope.locals.emplace(name, id);

        return id;
    }

    std::optional<LocalId> HirBuilder::resolveLocal(const std::string& name) const {
        for (auto scope = this->scopes.rbegin(); scope != this->scopes.rend(); ++scope) {
            if (auto it = scope->locals.find(name); it != scope->locals.end())
                return it->second;

            if (!scope->inherits)
                break;
        }

        return std::nullopt;
    }

    LocalId HirBuilder::captureLocal(const std::string& name, const Scope& source) {
        auto it = source.locals.find(name);
        if (it == source.locals.end())
            return invalidId;

        const LocalId sourceId = it->second;
        if (!source.body || !source.body->validLocal(sourceId))
            return invalidId;

        const Local& sourceLocal = source.body->locals[sourceId];
        const LocalId target = this->declareLocal(name, sourceLocal.type, sourceLocal.ownership);

        source.body->locals[sourceId].captured = true;
        this->captureStack.back().push_back(Capture{ name, sourceId, target });

        return target;
    }

    std::optional<LocalId> HirBuilder::resolveOrCapture(const std::string& name) {
        if (auto local = this->resolveLocal(name))
            return local;

        if (this->captureStack.empty())
            return std::nullopt;

        for (auto scope = this->scopes.rbegin() + 1; scope != this->scopes.rend(); ++scope) {
            if (scope->locals.contains(name))
                return this->captureLocal(name, *scope);
        }

        return std::nullopt;
    }

    std::optional<ValueNode::ValueType> HirBuilder::resolveConstant(const std::string& name) const {
        for (auto it = this->scopes.rbegin(); it != this->scopes.rend(); ++it) {
            if (auto found = it->constants.find(name); found != it->constants.end())
                return found->second;
        }

        return std::nullopt;
    }

    void HirBuilder::declareConstant(const std::string& name, const ValueNode::ValueType& value, const SourceLocation& loc) {
        Scope& scope = this->scopes.back();
        if (scope.constants.contains(name)) {
            this->diagnostics.addError(loc, "Constant '" + name + "' is already defined");
            return;
        }

        scope.constants.emplace(name, value);
    }

    std::optional<ValueNode::ValueType> HirBuilder::constantValue(ExprNode& node) const {
        if (node.getType() == ASTNode::Type::Value)
            return static_cast<ValueNode&>(node).value;

        if (node.getType() == ASTNode::Type::Array) {
            auto& array = static_cast<ArrayNode&>(node);
            auto result = std::make_shared<ArrayValue>();
            result->elements.reserve(array.elements.size());

            for (auto& element : array.elements) {
                auto value = this->constantValue(*element);
                if (!value)
                    return std::nullopt;
                result->elements.push_back(*value);
            }

            return result;
        }

        if (node.getType() == ASTNode::Type::Variable)
            return this->resolveConstant(this->qualifiedName(static_cast<VariableNode&>(node)));

        if (node.getType() == ASTNode::Type::Unary) {
            auto& unary = static_cast<UnaryNode&>(node);
            auto operand = this->constantValue(*unary.operand);
            if (!operand || unary.op != "-")
                return std::nullopt;

            if (std::holds_alternative<int>(*operand))
                return -std::get<int>(*operand);
            if (std::holds_alternative<float>(*operand))
                return -std::get<float>(*operand);

            return std::nullopt;
        }

        if (node.getType() == ASTNode::Type::Arithmetic) {
            auto& arithmetic = static_cast<ArithmeticNode&>(node);
            auto left = this->constantValue(*arithmetic.left);
            auto right = this->constantValue(*arithmetic.right);
            if (!left || !right)
                return std::nullopt;

            if (arithmetic.op == "+" && std::holds_alternative<std::string>(*left) &&
                std::holds_alternative<std::string>(*right))
                return std::get<std::string>(*left) + std::get<std::string>(*right);

            if (std::holds_alternative<int>(*left) && std::holds_alternative<int>(*right)) {
                const std::int64_t a = std::get<int>(*left);
                const std::int64_t b = std::get<int>(*right);
                std::int64_t result = 0;

                if (arithmetic.op == "+")
                    result = a + b;
                else if (arithmetic.op == "-")
                    result = a - b;
                else if (arithmetic.op == "*")
                    result = a * b;
                else if (arithmetic.op == "/" && b != 0)
                    result = a / b;
                else if (arithmetic.op == "%" && b != 0)
                    result = a % b;
                else
                    return std::nullopt;

                if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max())
                    return std::nullopt;

                return static_cast<int>(result);
            }

            if (std::holds_alternative<float>(*left) && std::holds_alternative<float>(*right)) {
                const float a = std::get<float>(*left);
                const float b = std::get<float>(*right);

                if (arithmetic.op == "+")
                    return a + b;
                if (arithmetic.op == "-")
                    return a - b;
                if (arithmetic.op == "*")
                    return a * b;
                if (arithmetic.op == "/" && b != 0.0f)
                    return a / b;

                return std::nullopt;
            }

            return std::nullopt;
        }

        return std::nullopt;
    }

    void HirBuilder::collectTopLevelConstant(ASTNode& node) {
        if (node.getType() != ASTNode::Type::Assignment)
            return;

        auto& decl = static_cast<AssignmentNode&>(node);
        if (!decl.isConst || !decl.value || decl.target->getType() != ASTNode::Type::Variable)
            return;

        this->hoistedConstants.insert(&decl);

        const std::string name = this->qualifiedName(static_cast<VariableNode&>(*decl.target));
        auto value = this->constantValue(*decl.value);
        if (!value) {
            this->diagnostics.addError(decl.loc, "Constant '" + name + "' requires a constant expression");
            return;
        }

        this->declareConstant(name, *value, decl.loc);
    }

    ty::Ty HirBuilder::lowerType(const TypeInfo& type) const {
        return this->program.types->lower(type);
    }

    int HirBuilder::fieldSlotOf(const TypeInfo& owner, const std::string& name) const {
        if (owner.kind != TypeKind::Object)
            return -1;

        auto classIt = this->program.classIndices.find(owner.className);
        if (classIt != this->program.classIndices.end()) {
            const auto& fields = this->program.classes[static_cast<std::size_t>(classIt->second)].fields;
            auto it = std::ranges::find(fields, name, &ty::FieldDef::name);
            return it == fields.end() ? -1 : static_cast<int>(std::distance(fields.begin(), it));
        }

        const auto fields = ClassCall::getInstance().getFields(owner.className);
        auto it = std::ranges::find(fields, name);
        return it == fields.end() ? -1 : static_cast<int>(std::distance(fields.begin(), it));
    }

    ExprId HirBuilder::addExpr(Expr expr) {
        this->current->exprs.push_back(std::move(expr));
        return static_cast<ExprId>(this->current->exprs.size() - 1);
    }

    StmtId HirBuilder::addStmt(Stmt stmt) {
        this->current->stmts.push_back(std::move(stmt));
        return static_cast<StmtId>(this->current->stmts.size() - 1);
    }

    ExprId HirBuilder::literalExpr(const ValueNode::ValueType& value, const SourceLocation& loc, ty::Ty type) {
        Expr expr;
        expr.kind = ExprKind::Literal;
        expr.type = type.valid() ? type : literalType(*this->program.types, value);
        expr.loc = loc;
        expr.literal = value;
        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::localRef(LocalId id, const SourceLocation& loc) {
        Expr expr;
        expr.kind = ExprKind::LocalRef;
        expr.loc = loc;
        expr.ordinal = static_cast<int>(id);

        if (this->current->validLocal(id))
            expr.type = this->current->locals[id].type;
        else
            expr.type = this->program.types->dynamic();

        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::globalRef(const std::string& name, const SourceLocation& loc) {
        Expr expr;
        expr.kind = ExprKind::GlobalRef;
        expr.type = this->program.types->dynamic();
        expr.loc = loc;
        expr.name = name;
        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::defaultExpr(const SourceLocation& loc) {
        return this->literalExpr(std::string(""), loc, this->program.types->primitive(ty::TyKind::String));
    }

    std::uint32_t HirBuilder::appendBody() {
        this->program.bodies.emplace_back();
        return static_cast<std::uint32_t>(this->program.bodies.size() - 1);
    }

    namespace detail {
        bool isDefinition(ASTNode& node) {
            switch (node.getType()) {
                case ASTNode::Type::Class:
                case ASTNode::Type::Enum:
                case ASTNode::Type::FunctionDef:
                case ASTNode::Type::Using:
                case ASTNode::Type::Trait:
                case ASTNode::Type::Impl:
                case ASTNode::Type::Import:
                case ASTNode::Type::Component:
                    return true;
                default:
                    return false;
            }
        }

        bool isDeclaration(ASTNode& node) {
            return node.getType() == ASTNode::Type::Assignment &&
                   (static_cast<AssignmentNode&>(node).isDeclaration || static_cast<AssignmentNode&>(node).isConst);
        }
    }

    Program HirBuilder::build(ASTNode& root) {
        this->program.types = std::make_shared<ty::TypeContext>();
        this->current = &this->program.topLevel;
        this->program.topLevel.isTopLevel = true;
        this->pushScope(false, false);

        if (root.getType() == ASTNode::Type::Program) {
            auto& programNode = static_cast<ProgramNode&>(root);

            for (auto& part : programNode.parts)
                this->collectTopLevelConstant(*part);

            for (auto& part : programNode.parts) {
                if (part->getType() == ASTNode::Type::Class) {
                    auto& cls = static_cast<ClassNode&>(*part);
                    this->classNodes.insert_or_assign(cls.name, std::ref(cls));
                }
            }

            for (auto& part : programNode.parts) {
                switch (part->getType()) {
                    case ASTNode::Type::Class:
                        this->registerClassMeta(static_cast<ClassNode&>(*part));
                        break;
                    case ASTNode::Type::Enum:
                        this->registerEnumMeta(static_cast<EnumDeclNode&>(*part));
                        break;
                    case ASTNode::Type::FunctionDef:
                        this->registerFunctionMeta(static_cast<FunctionDefNode&>(*part));
                        break;
                    default:
                        break;
                }
            }

            for (std::size_t index = 0; index < this->bodyOrder.size(); ++index) {
                ASTNode& target = this->bodyOrder[index].get();
                if (target.getType() == ASTNode::Type::Class)
                    this->buildClassBodies(static_cast<ClassNode&>(target));
                else if (target.getType() == ASTNode::Type::FunctionDef)
                    this->buildFunctionBody(static_cast<FunctionDefNode&>(target));
            }

            for (auto& part : programNode.parts) {
                if (isDefinition(*part))
                    continue;

                StmtId stmt = this->buildStmt(*part);
                if (stmt != invalidId)
                    this->program.topLevel.root.push_back(stmt);

                this->lastPartWasDeclaration = isDeclaration(*part);
            }
        } else {
            StmtId stmt = this->buildStmt(root);
            if (stmt != invalidId)
                this->program.topLevel.root.push_back(stmt);
        }

        this->program.topLevel.returnsValue = !this->lastPartWasDeclaration;

        this->popScope();
        this->program.types->finalize();

        return std::move(this->program);
    }

    void HirBuilder::registerClassMeta(ClassNode& node) {
        if (this->program.classIndices.contains(node.name))
            return;

        int baseIndex = -1;
        if (!node.baseClassName.empty()) {
            if (this->registeringClasses.contains(node.name)) {
                this->diagnostics.addError(node.loc, "Circular inheritance involving class '" + node.name + "'");
                return;
            }

            this->registeringClasses.insert(node.name);

            auto baseIt = this->classNodes.find(node.baseClassName);
            if (baseIt == this->classNodes.end()) {
                this->diagnostics.addError(node.loc, "Unknown base class: " + node.baseClassName);
                this->registeringClasses.erase(node.name);
                return;
            }

            if (!this->program.classIndices.contains(node.baseClassName))
                this->registerClassMeta(baseIt->second.get());

            auto registered = this->program.classIndices.find(node.baseClassName);
            if (registered == this->program.classIndices.end()) {
                this->diagnostics.addError(node.loc, "Failed to register base class: " + node.baseClassName);
                this->registeringClasses.erase(node.name);
                return;
            }

            baseIndex = registered->second;
            this->registeringClasses.erase(node.name);
        }

        Class cls;
        cls.name = node.name;
        cls.baseName = node.baseClassName;
        cls.classIndex = static_cast<int>(this->program.classes.size());
        cls.type = this->program.types->adt(node.name);

        if (baseIndex >= 0) {
            const Class& base = this->program.classes[static_cast<std::size_t>(baseIndex)];
            cls.fields = base.fields;
            cls.staticFields = base.staticFields;
            cls.methodTable = base.methodTable;
            cls.methodSignatures = base.methodSignatures;
            cls.staticMethodTable = base.staticMethodTable;
            cls.staticMethodSignatures = base.staticMethodSignatures;
            cls.ancestors = base.ancestors;
            cls.ancestors.insert(cls.ancestors.begin(), baseIndex);
            cls.ancestorMask = base.ancestorMask;
            if (baseIndex < 64)
                cls.ancestorMask |= std::uint64_t{ 1 } << baseIndex;
            cls.constructorOrdinal = base.constructorOrdinal;
        }

        for (const ClassMember& member : node.members) {
            auto& fields = member.isStatic ? cls.staticFields : cls.fields;
            auto it = std::ranges::find(fields, member.name, &ty::FieldDef::name);

            if (it == fields.end()) {
                ty::FieldDef field;
                field.name = member.name;
                field.type = this->lowerType(member.type);
                fields.push_back(std::move(field));
                it = std::prev(fields.end());
            }

            it->type = this->lowerType(member.type);
            it->hasDefault = member.hasDefault;

            if (member.hasDefault && member.defaultExpr) {
                auto value = this->constantValue(*member.defaultExpr);
                if (value) {
                    it->defaultValue = *value;
                } else {
                    this->diagnostics.addError(node.loc,
                        "Member default value of '" + member.name + "' must be a constant literal");
                    it->defaultValue = ValueNode::ValueType{};
                }
            } else {
                it->defaultValue = ValueNode::ValueType{};
            }
        }

        MethodLayoutTable table;
        if (baseIndex >= 0) {
            table.order = cls.methodSignatures;
            table.staticOrder = cls.staticMethodSignatures;
        }
        extendMethodTable(table, node.methods);

        cls.methodSignatures = table.order;
        cls.staticMethodSignatures = table.staticOrder;
        cls.methodTable.assign(cls.methodSignatures.size(), -1);
        cls.staticMethodTable.assign(cls.staticMethodSignatures.size(), -1);

        if (baseIndex >= 0) {
            const Class& base = this->program.classes[static_cast<std::size_t>(baseIndex)];
            for (std::size_t index = 0; index < cls.methodTable.size() && index < base.methodTable.size(); ++index)
                cls.methodTable[index] = base.methodTable[index];
            for (std::size_t index = 0; index < cls.staticMethodTable.size() && index < base.staticMethodTable.size(); ++index)
                cls.staticMethodTable[index] = base.staticMethodTable[index];
        }

        for (const MethodDecl& method : node.methods) {
            const int ordinal = static_cast<int>(this->methodCount++);

            Method entry;
            entry.name = method.name;
            entry.signature = methodSignatureString(method);
            entry.result = this->lowerType(method.returnType);
            entry.ordinal = ordinal;
            entry.classIndex = cls.classIndex;
            entry.isConstructor = method.isConstructor;
            entry.isStatic = method.isStatic;

            for (std::size_t index = 0; index < method.params.size(); ++index) {
                Param param;
                param.name = method.params[index].name;
                if (index < method.paramTypes.size())
                    param.type = this->lowerType(method.paramTypes[index]);
                else if (method.params[index].hasType)
                    param.type = lowerTypeExpr(*this->program.types, method.params[index].typeExpr);
                else
                    param.type = this->program.types->dynamic();
                param.ownership = index < method.paramTypes.size() ? method.paramTypes[index].ownership : Ownership::Value;
                entry.params.push_back(std::move(param));
            }

            if (method.isConstructor) {
                cls.constructorOrdinal = ordinal;
            } else {
                const auto& signatures = method.isStatic ? cls.staticMethodSignatures : cls.methodSignatures;
                auto& target = method.isStatic ? cls.staticMethodTable : cls.methodTable;
                auto it = std::find(signatures.begin(), signatures.end(), entry.signature);
                if (it != signatures.end())
                    target[static_cast<std::size_t>(std::distance(signatures.begin(), it))] = ordinal;
            }

            cls.methods.push_back(std::move(entry));
        }

        this->classMethodIndices[node.name] = cls.methodTable;
        this->program.classIndices[node.name] = cls.classIndex;
        this->program.classes.push_back(std::move(cls));
        this->bodyOrder.push_back(node);
    }

    void HirBuilder::registerFunctionMeta(FunctionDefNode& node) {
        Function function;
        function.name = node.name;
        function.ordinal = static_cast<int>(this->methodCount++);
        function.result = this->lowerType(node.decl.returnType);

        for (std::size_t index = 0; index < node.decl.params.size(); ++index) {
            Param param;
            param.name = node.decl.params[index].name;
            if (index < node.decl.paramTypes.size())
                param.type = this->lowerType(node.decl.paramTypes[index]);
            else if (node.decl.params[index].hasType)
                param.type = lowerTypeExpr(*this->program.types, node.decl.params[index].typeExpr);
            else
                param.type = this->program.types->dynamic();
            param.ownership = index < node.decl.paramTypes.size() ? node.decl.paramTypes[index].ownership : Ownership::Value;
            function.params.push_back(std::move(param));
        }

        this->program.functionOrdinals[node.name].push_back(function.ordinal);
        this->program.functions.push_back(std::move(function));
        this->bodyOrder.push_back(node);
    }

    void HirBuilder::registerEnumMeta(EnumDeclNode& node) {
        if (this->program.enumIndices.contains(node.name))
            return;

        Enum entry;
        entry.name = node.name;
        entry.enumIndex = static_cast<int>(this->program.enums.size());

        for (std::size_t tag = 0; tag < node.variants.size(); ++tag) {
            const ::LOICollection::frontend::EnumVariant& variant = node.variants[tag];

            EnumVariant compiled;
            compiled.name = variant.name;
            compiled.payload.reserve(variant.fieldTypes.size());
            for (const TypeExpr& field : variant.fieldTypes)
                compiled.payload.push_back(lowerTypeExpr(*this->program.types, field));

            this->program.enumVariants.emplace(variant.name,
                std::make_pair(entry.enumIndex, static_cast<int>(tag)));
            entry.variants.push_back(std::move(compiled));
        }

        this->program.enumIndices[node.name] = entry.enumIndex;
        this->program.enums.push_back(std::move(entry));
    }

    void HirBuilder::buildClassBodies(ClassNode& node) {
        auto classIt = this->program.classIndices.find(node.name);
        if (classIt == this->program.classIndices.end())
            return;

        Class& cls = this->program.classes[static_cast<std::size_t>(classIt->second)];

        for (std::size_t index = 0; index < node.methods.size() && index < cls.methods.size(); ++index) {
            const std::uint32_t bodyIndex = this->appendBody();
            cls.methods[index].bodyIndex = bodyIndex;
            this->buildMethodBody(node, node.methods[index], cls.classIndex, cls.methods[index].ordinal, bodyIndex);
        }
    }

    void HirBuilder::buildFunctionBody(FunctionDefNode& node) {
        Function* function = nullptr;
        for (Function& candidate : this->program.functions) {
            if (candidate.name == node.name && candidate.bodyIndex == invalidId) {
                function = &candidate;
                break;
            }
        }

        if (!function) {
            this->diagnostics.addError(node.loc, "Unresolved function body: " + node.name);
            return;
        }

        const std::uint32_t bodyIndex = this->appendBody();
        function->bodyIndex = bodyIndex;

        Body& body = this->program.bodies[bodyIndex];
        Body* saved = this->current;
        this->current = &body;
        this->lastPartWasDeclaration = false;

        this->pushScope(false, false);
        for (const Param& param : function->params)
            this->declareLocal(param.name, param.type, param.ownership, true);
        body.paramCount = static_cast<std::uint32_t>(function->params.size());

        if (node.decl.body)
            body.root = this->buildBodyOf(*node.decl.body);

        body.returnsValue = !this->lastPartWasDeclaration;

        this->popScope();
        this->current = saved;
    }

    void HirBuilder::buildMethodBody(
        ClassNode& node, MethodDecl& method, int classIndex, int ordinal, std::uint32_t bodyIndex
    ) {
        Body& body = this->program.bodies[bodyIndex];
        Body* saved = this->current;
        this->current = &body;
        this->lastPartWasDeclaration = false;

        this->pushScope(true, false);

        const Class& cls = this->program.classes[static_cast<std::size_t>(classIndex)];
        const Method* entry = nullptr;
        for (const Method& candidate : cls.methods) {
            if (candidate.ordinal == ordinal) {
                entry = &candidate;
                break;
            }
        }

        const std::vector<Param> empty;
        const std::vector<Param>& params = entry ? entry->params : empty;
        for (std::size_t index = 0; index < method.params.size(); ++index) {
            if (index < params.size())
                this->declareLocal(params[index].name, params[index].type, params[index].ownership, true);
            else if (method.params[index].hasType)
                this->declareLocal(method.params[index].name,
                    lowerTypeExpr(*this->program.types, method.params[index].typeExpr), Ownership::Value, true);
            else
                this->declareLocal(method.params[index].name, this->program.types->dynamic(), Ownership::Value, true);
        }
        body.paramCount = static_cast<std::uint32_t>(method.params.size());

        if (method.isConstructor && !node.baseClassName.empty() && !method.hasSuperCall) {
            int constructor = -1;
            auto baseIt = this->program.classIndices.find(node.baseClassName);
            int walk = baseIt == this->program.classIndices.end() ? -1 : baseIt->second;

            while (walk >= 0) {
                const Class& parent = this->program.classes[static_cast<std::size_t>(walk)];
                if (parent.constructorOrdinal != -1) {
                    constructor = parent.constructorOrdinal;
                    break;
                }

                auto next = this->program.classIndices.find(parent.baseName);
                walk = next == this->program.classIndices.end() ? -1 : next->second;
            }

            Expr expr;
            expr.kind = ExprKind::SuperCall;
            expr.type = this->program.types->primitive(ty::TyKind::Void);
            expr.loc = node.loc;
            expr.className = node.baseClassName;
            expr.ordinal = constructor;

            Stmt stmt;
            stmt.kind = StmtKind::Expr;
            stmt.loc = node.loc;
            stmt.value = this->addExpr(std::move(expr));
            body.root.push_back(this->addStmt(std::move(stmt)));
        }

        if (method.body) {
            auto statements = this->buildBodyOf(*method.body);
            body.root.insert(body.root.end(), statements.begin(), statements.end());
        }

        body.returnsValue = !this->lastPartWasDeclaration;

        this->popScope();
        this->current = saved;
    }

    std::vector<StmtId> HirBuilder::buildBodyOf(ASTNode& node) {
        if (node.getType() == ASTNode::Type::Program)
            return this->buildSequence(static_cast<ProgramNode&>(node));
        if (node.getType() == ASTNode::Type::Block)
            return this->buildSequence(static_cast<BlockNode&>(node));

        std::vector<StmtId> result;
        StmtId stmt = this->buildStmt(node);
        if (stmt != invalidId)
            result.push_back(stmt);

        this->lastPartWasDeclaration = isDeclaration(node);
        return result;
    }

    std::vector<StmtId> HirBuilder::buildSequence(SequenceNode& node) {
        std::vector<StmtId> result;

        for (auto& part : node.parts) {
            if (isDefinition(*part)) {
                this->lastPartWasDeclaration = false;
                continue;
            }

            StmtId stmt = this->buildStmt(*part);
            if (stmt != invalidId)
                result.push_back(stmt);

            this->lastPartWasDeclaration = isDeclaration(*part);
        }

        return result;
    }

    StmtId HirBuilder::buildStmt(ASTNode& node) {
        switch (node.getType()) {
            case ASTNode::Type::Assignment:
                return this->buildAssignment(static_cast<AssignmentNode&>(node));
            case ASTNode::Type::CompoundAssign:
                return this->buildCompoundAssign(static_cast<CompoundAssignNode&>(node));
            case ASTNode::Type::If: {
                auto& ifNode = static_cast<IfNode&>(node);
                Stmt stmt;
                stmt.kind = StmtKind::Expr;
                stmt.loc = ifNode.loc;
                stmt.value = this->buildIfExpr(ifNode);
                return stmt.value == invalidId ? invalidId : this->addStmt(std::move(stmt));
            }
            case ASTNode::Type::While:
                return this->buildWhile(static_cast<WhileNode&>(node));
            case ASTNode::Type::For:
                return this->buildFor(static_cast<ForNode&>(node));
            case ASTNode::Type::ForIn:
                return this->buildForIn(static_cast<ForInNode&>(node));
            case ASTNode::Type::Match:
                return this->buildMatch(static_cast<MatchNode&>(node));
            case ASTNode::Type::Return:
                return this->buildReturn(static_cast<ReturnNode&>(node));
            case ASTNode::Type::Panic:
                return this->buildPanic(static_cast<PanicNode&>(node));
            case ASTNode::Type::Block:
                return this->buildBlock(static_cast<BlockNode&>(node));
            case ASTNode::Type::Break: {
                Stmt stmt;
                stmt.kind = StmtKind::Break;
                stmt.loc = static_cast<BreakNode&>(node).loc;
                return this->addStmt(std::move(stmt));
            }
            case ASTNode::Type::Continue: {
                Stmt stmt;
                stmt.kind = StmtKind::Continue;
                stmt.loc = static_cast<ContinueNode&>(node).loc;
                return this->addStmt(std::move(stmt));
            }
            case ASTNode::Type::Class:
            case ASTNode::Type::Enum:
            case ASTNode::Type::FunctionDef:
            case ASTNode::Type::Using:
            case ASTNode::Type::Trait:
            case ASTNode::Type::Impl:
            case ASTNode::Type::Import:
            case ASTNode::Type::Component:
                return invalidId;
            default:
                break;
        }

        ExprId expr = this->buildExpr(static_cast<ExprNode&>(node));
        if (expr == invalidId)
            return invalidId;

        Stmt stmt;
        stmt.kind = StmtKind::Expr;
        stmt.loc = this->current->exprs[expr].loc;
        stmt.value = expr;
        return this->addStmt(std::move(stmt));
    }

    StmtId HirBuilder::buildAssignment(AssignmentNode& node) {
        if (node.isConst) {
            if (!this->hoistedConstants.contains(&node) && node.value &&
                node.target->getType() == ASTNode::Type::Variable) {
                const std::string name = this->qualifiedName(static_cast<VariableNode&>(*node.target));
                auto value = this->constantValue(*node.value);
                if (value)
                    this->declareConstant(name, *value, node.loc);
                else
                    this->diagnostics.addError(node.loc, "Constant '" + name + "' requires a constant expression");
            }

            return invalidId;
        }

        if (node.isDeclaration && node.value && node.target->getType() == ASTNode::Type::Variable) {
            auto& var = static_cast<VariableNode&>(*node.target);
            if (!var.isStaticField && !this->current->isTopLevel &&
                node.value->getType() == ASTNode::Type::New &&
                static_cast<NewNode&>(*node.value).declarativeBlock &&
                !static_cast<NewNode&>(*node.value).receiverName.empty()) {
                const ty::Ty type = node.hasDeclaredType
                    ? this->lowerType(node.declaredTypeInfo)
                    : this->lowerType(node.value->type);
                this->declareLocal(var.name, type, node.declaredTypeInfo.ownership);
            }
        }

        const ExprId value = node.value ? this->buildExpr(*node.value) : this->defaultExpr(node.loc);
        if (value == invalidId)
            return invalidId;

        ExprId target = invalidId;

        switch (node.target->getType()) {
            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(*node.target);
                const std::string name = this->qualifiedName(var);

                if (this->resolveConstant(name)) {
                    this->diagnostics.addError(node.loc, "Cannot assign to constant '" + name + "'");
                    return invalidId;
                }

                if (auto local = this->resolveOrCapture(name)) {
                    target = this->localRef(*local, node.loc);
                } else if (!var.isStaticField && !this->current->isTopLevel && node.isDeclaration) {
                    const ty::Ty type = node.hasDeclaredType
                        ? this->lowerType(node.declaredTypeInfo)
                        : this->current->exprs[value].type;
                    const LocalId created = this->declareLocal(var.name, type, node.declaredTypeInfo.ownership);
                    target = this->localRef(created, node.loc);
                } else {
                    target = this->globalRef(name, node.loc);
                }
                break;
            }
            case ASTNode::Type::MemberAccess:
                target = this->buildExpr(*node.target);
                break;
            case ASTNode::Type::Index:
                target = this->buildExpr(*node.target);
                break;
            default:
                this->diagnostics.addError(node.loc, "Invalid assignment target");
                return invalidId;
        }

        if (target == invalidId)
            return invalidId;

        Stmt stmt;
        stmt.kind = node.isDeclaration ? StmtKind::Let : StmtKind::Assign;
        stmt.loc = node.loc;
        stmt.value = value;
        stmt.target = target;
        return this->addStmt(std::move(stmt));
    }

    StmtId HirBuilder::buildCompoundAssign(CompoundAssignNode& node) {
        ExprId target = invalidId;

        switch (node.target->getType()) {
            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(*node.target);
                const std::string name = this->qualifiedName(var);
                if (this->resolveConstant(name)) {
                    this->diagnostics.addError(node.loc, "Cannot assign to constant '" + name + "'");
                    return invalidId;
                }
                if (auto local = this->resolveOrCapture(name))
                    target = this->localRef(*local, node.loc);
                else
                    target = this->globalRef(name, node.loc);
                break;
            }
            case ASTNode::Type::MemberAccess:
            case ASTNode::Type::Index:
                target = this->buildExpr(*node.target);
                break;
            default:
                this->diagnostics.addError(node.loc, "Invalid compound assignment target");
                return invalidId;
        }

        const ExprId value = this->buildExpr(*node.value);
        if (target == invalidId || value == invalidId)
            return invalidId;

        Stmt stmt;
        stmt.kind = StmtKind::CompoundAssign;
        stmt.loc = node.loc;
        stmt.value = value;
        stmt.target = target;
        stmt.op = node.op;
        return this->addStmt(std::move(stmt));
    }

    StmtId HirBuilder::buildWhile(WhileNode& node) {
        const bool bindsPattern = node.condition &&
            node.condition->getType() == ASTNode::Type::LetPattern;

        Stmt stmt;
        stmt.kind = StmtKind::While;
        stmt.loc = node.loc;

        if (bindsPattern) {
            this->pushScope(false, true);

            stmt.condition = this->buildExpr(*node.condition);
            auto& pattern = static_cast<LetPatternNode&>(*node.condition);
            if (this->current->validExpr(stmt.condition)) {
                Expr& expr = this->current->exprs[stmt.condition];
                for (const std::string& binder : pattern.binders) {
                    const LocalId local = this->declareLocal(binder, this->program.types->dynamic(), Ownership::Value);
                    expr.binderLocals.push_back(local);
                }
            }

            if (node.body)
                stmt.body = this->buildBodyOf(*node.body);

            this->popScope();
        } else {
            stmt.condition = this->buildExpr(*node.condition);
            if (node.body)
                stmt.body = this->buildBodyOf(*node.body);
        }

        return this->addStmt(std::move(stmt));
    }

    StmtId HirBuilder::buildFor(ForNode& node) {
        std::vector<StmtId> prefix;

        Stmt stmt;
        stmt.kind = StmtKind::For;
        stmt.loc = node.loc;

        if (node.init) {
            if (node.init->getType() == ASTNode::Type::Assignment &&
                static_cast<AssignmentNode&>(*node.init).isDeclaration) {
                const StmtId declaration = this->buildAssignment(static_cast<AssignmentNode&>(*node.init));
                if (declaration != invalidId)
                    prefix.push_back(declaration);
            } else {
                stmt.target = this->buildExpr(*node.init);
            }
        }

        if (node.condition)
            stmt.condition = this->buildExpr(*node.condition);
        if (node.step)
            stmt.value = this->buildExpr(*node.step);
        if (node.body)
            stmt.body = this->buildBodyOf(*node.body);

        const StmtId loop = this->addStmt(std::move(stmt));
        if (prefix.empty())
            return loop;

        Stmt block;
        block.kind = StmtKind::Block;
        block.loc = node.loc;
        block.body = std::move(prefix);
        block.body.push_back(loop);
        return this->addStmt(std::move(block));
    }

    StmtId HirBuilder::buildForIn(ForInNode& node) {
        ClassLookup lookup = [this](const std::string& name) -> ClassNode* {
            auto it = this->classNodes.find(name);
            return it == this->classNodes.end() ? nullptr : &it->second.get();
        };

        auto protocol = iterableProtocol(node.iterable->getType(), node.iterable->type, lookup);
        if (!protocol) {
            this->diagnostics.addError(node.loc, "for-in iterable does not provide an iteration protocol");
            return invalidId;
        }

        Stmt stmt;
        stmt.kind = StmtKind::ForIn;
        stmt.loc = node.loc;
        stmt.value = this->buildExpr(*node.iterable);
        stmt.name = node.elementVar;
        stmt.op = node.indexVar;
        stmt.flag = node.hasIndexVar;
        stmt.shape = static_cast<int>(protocol->shape);
        stmt.className = protocol->className;

        const ty::Ty elementType = protocol->elementType.kind == TypeKind::Unknown
            ? this->program.types->dynamic()
            : this->lowerType(protocol->elementType);
        stmt.elementLocal = this->declareLocal(node.elementVar, elementType, Ownership::Value);
        if (node.hasIndexVar)
            stmt.indexLocal = this->declareLocal(node.indexVar, this->program.types->primitive(ty::TyKind::Int), Ownership::Value);

        if (node.body)
            stmt.body = this->buildBodyOf(*node.body);

        return this->addStmt(std::move(stmt));
    }

    StmtId HirBuilder::buildMatch(MatchNode& node) {
        Stmt stmt;
        stmt.kind = StmtKind::Expr;
        stmt.loc = node.loc;
        stmt.value = this->buildMatchExpr(node);
        return this->addStmt(std::move(stmt));
    }

    StmtId HirBuilder::buildReturn(ReturnNode& node) {
        Stmt stmt;
        stmt.kind = StmtKind::Return;
        stmt.loc = node.loc;
        stmt.value = node.value ? this->buildExpr(*node.value) : invalidId;
        return this->addStmt(std::move(stmt));
    }

    StmtId HirBuilder::buildPanic(PanicNode& node) {
        Stmt stmt;
        stmt.kind = StmtKind::Panic;
        stmt.loc = node.loc;
        stmt.value = node.message ? this->buildExpr(*node.message) : invalidId;
        return this->addStmt(std::move(stmt));
    }

    StmtId HirBuilder::buildBlock(BlockNode& node) {
        Stmt stmt;
        stmt.kind = StmtKind::Block;
        stmt.loc = {};
        stmt.body = this->buildSequence(node);
        return this->addStmt(std::move(stmt));
    }

    std::vector<StmtId> HirBuilder::buildDeclarativeBlock(BlockNode& block, LocalId receiver) {
        this->declarativeReceivers.push_back(receiver);
        auto statements = this->buildSequence(block);
        this->declarativeReceivers.pop_back();
        return statements;
    }

    ExprId HirBuilder::buildExpr(ExprNode& node) {
        const ExprId id = this->buildExprImpl(node);
        if (id != invalidId)
            this->current->exprs[id].flag2 = node.preserveOptional;
        return id;
    }

    ExprId HirBuilder::buildExprImpl(ExprNode& node) {
        switch (node.getType()) {
            case ASTNode::Type::Value:
                return this->buildValue(static_cast<ValueNode&>(node));
            case ASTNode::Type::Variable:
                return this->buildVariable(static_cast<VariableNode&>(node));
            case ASTNode::Type::This: {
                Expr expr;
                expr.kind = ExprKind::This;
                expr.type = this->lowerType(node.type);
                expr.loc = static_cast<ThisNode&>(node).loc;
                return this->addExpr(std::move(expr));
            }
            case ASTNode::Type::Super: {
                Expr expr;
                expr.kind = ExprKind::Super;
                expr.type = this->lowerType(node.type);
                expr.loc = static_cast<SuperNode&>(node).loc;
                return this->addExpr(std::move(expr));
            }
            case ASTNode::Type::MemberAccess:
                return this->buildMemberAccess(static_cast<MemberAccessNode&>(node));
            case ASTNode::Type::Index:
                return this->buildIndex(static_cast<IndexAccessNode&>(node));
            case ASTNode::Type::Function:
                return this->buildFunction(static_cast<FunctionNode&>(node));
            case ASTNode::Type::Macro:
                return this->buildMacro(static_cast<MacroNode&>(node));
            case ASTNode::Type::FuncCall:
                return this->buildFuncCall(static_cast<FuncCallNode&>(node));
            case ASTNode::Type::MethodCall:
                return this->buildMethodCall(static_cast<MethodCallNode&>(node));
            case ASTNode::Type::New:
                return this->buildNew(static_cast<NewNode&>(node));
            case ASTNode::Type::Lambda:
                return this->buildLambda(static_cast<LambdaNode&>(node));
            case ASTNode::Type::Array:
                return this->buildArray(static_cast<ArrayNode&>(node));
            case ASTNode::Type::Arithmetic:
                return this->buildArithmetic(static_cast<ArithmeticNode&>(node));
            case ASTNode::Type::Unary:
                return this->buildUnary(static_cast<UnaryNode&>(node));
            case ASTNode::Type::Compare:
                return this->buildCompare(static_cast<CompareNode&>(node));
            case ASTNode::Type::Logical:
                return this->buildLogical(static_cast<LogicalNode&>(node));
            case ASTNode::Type::Coalesce:
                return this->buildCoalesce(static_cast<CoalesceNode&>(node));
            case ASTNode::Type::Try:
                return this->buildTry(static_cast<TryNode&>(node));
            case ASTNode::Type::Range:
                return this->buildRange(static_cast<RangeNode&>(node));
            case ASTNode::Type::If:
                return this->buildIfExpr(static_cast<IfNode&>(node));
            case ASTNode::Type::Match:
                return this->buildMatchExpr(static_cast<MatchNode&>(node));
            case ASTNode::Type::LetPattern:
                return this->buildLetPattern(static_cast<LetPatternNode&>(node));
            case ASTNode::Type::InstanceOf: {
                auto& instance = static_cast<InstanceOfNode&>(node);
                Expr expr;
                expr.kind = ExprKind::InstanceOf;
                expr.type = this->lowerType(node.type);
                expr.loc = instance.loc;
                expr.className = instance.className;
                expr.args.push_back(this->buildExpr(*instance.target));
                return this->addExpr(std::move(expr));
            }
            case ASTNode::Type::SuperCall: {
                auto& call = static_cast<SuperCallNode&>(node);
                Expr expr;
                expr.kind = ExprKind::SuperCall;
                expr.type = this->lowerType(node.type);
                expr.loc = call.loc;
                expr.className = call.className;
                expr.ordinal = call.constructorIndex;
                for (auto& arg : call.args)
                    expr.args.push_back(this->buildExpr(*arg));
                return this->addExpr(std::move(expr));
            }
            case ASTNode::Type::Assignment: {
                auto& assign = static_cast<AssignmentNode&>(node);
                ExprId target = invalidId;
                if (assign.target->getType() == ASTNode::Type::Variable) {
                    auto& var = static_cast<VariableNode&>(*assign.target);
                    const std::string name = this->qualifiedName(var);
                    if (auto local = this->resolveOrCapture(name))
                        target = this->localRef(*local, assign.loc);
                    else
                        target = this->globalRef(name, assign.loc);
                } else {
                    target = this->buildExpr(*assign.target);
                }

                Expr expr;
                expr.kind = ExprKind::Assign;
                expr.type = this->lowerType(node.type);
                expr.loc = assign.loc;
                expr.args.push_back(assign.value ? this->buildExpr(*assign.value) : this->defaultExpr(assign.loc));
                expr.args.push_back(target);
                return this->addExpr(std::move(expr));
            }
            case ASTNode::Type::CompoundAssign: {
                auto& assign = static_cast<CompoundAssignNode&>(node);
                ExprId target = invalidId;
                if (assign.target->getType() == ASTNode::Type::Variable) {
                    auto& var = static_cast<VariableNode&>(*assign.target);
                    const std::string name = this->qualifiedName(var);
                    if (auto local = this->resolveOrCapture(name))
                        target = this->localRef(*local, assign.loc);
                    else
                        target = this->globalRef(name, assign.loc);
                } else {
                    target = this->buildExpr(*assign.target);
                }

                Expr expr;
                expr.kind = ExprKind::CompoundAssign;
                expr.type = this->lowerType(node.type);
                expr.loc = assign.loc;
                expr.op = assign.op;
                expr.args.push_back(this->buildExpr(*assign.value));
                expr.args.push_back(target);
                return this->addExpr(std::move(expr));
            }
            default:
                this->diagnostics.addError({}, "Unsupported expression in HIR");
                return invalidId;
        }
    }

    ExprId HirBuilder::buildValue(ValueNode& node) {
        ty::Ty type = this->lowerType(node.type);
        if (node.type.kind == TypeKind::Unknown)
            type = literalType(*this->program.types, node.value);

        return this->literalExpr(node.value, node.loc, type);
    }

    ExprId HirBuilder::buildVariable(VariableNode& node) {
        const std::string name = this->qualifiedName(node);

        if (auto constant = this->resolveConstant(name)) {
            ExprId id = this->literalExpr(*constant, node.loc, this->lowerType(node.type));
            this->current->exprs[id].flag2 = node.preserveOptional;
            return id;
        }

        ExprId id = invalidId;
        if (auto local = this->resolveOrCapture(name))
            id = this->localRef(*local, node.loc);
        else
            id = this->globalRef(name, node.loc);

        if (node.type.kind != TypeKind::Unknown)
            this->current->exprs[id].type = this->lowerType(node.type);

        this->current->exprs[id].flag2 = node.preserveOptional;
        return id;
    }

    ExprId HirBuilder::buildMemberAccess(MemberAccessNode& node) {
        if (node.isStaticAccess) {
            ExprId id = this->globalRef(this->qualifiedName(node), node.loc);
            if (node.type.kind != TypeKind::Unknown)
                this->current->exprs[id].type = this->lowerType(node.type);
            this->current->exprs[id].flag2 = node.preserveOptional;
            return id;
        }

        const ExprId target = this->buildExpr(*node.target);
        const int slot = this->fieldSlotOf(node.target->type, node.memberName);

        Expr expr;
        expr.kind = node.isSafe && node.memberKind == MemberAccessNode::MemberKind::Normal
            ? ExprKind::SafeField
            : ExprKind::Field;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.name = node.memberName;
        expr.slot = slot;
        expr.args.push_back(target);
        expr.flag = node.isSafe;
        expr.flag2 = node.preserveOptional;

        switch (node.memberKind) {
            case MemberAccessNode::MemberKind::TypeOf:
                expr.kind = ExprKind::TypeOf;
                break;
            case MemberAccessNode::MemberKind::HasValue:
                expr.kind = ExprKind::HasValue;
                break;
            case MemberAccessNode::MemberKind::Value:
                if (!node.isSafe) {
                    if (node.target->type.kind != TypeKind::Optional)
                        return target;
                    expr.kind = ExprKind::Unwrap;
                } else {
                    expr.kind = ExprKind::Unwrap;
                }
                break;
            case MemberAccessNode::MemberKind::Normal:
                break;
        }

        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildIndex(IndexAccessNode& node) {
        Expr expr;
        expr.kind = node.isSafe ? ExprKind::SafeIndex : ExprKind::Index;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.args.push_back(this->buildExpr(*node.target));
        expr.args.push_back(this->buildExpr(*node.index));
        expr.flag2 = node.preserveOptional;
        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildFunction(FunctionNode& node) {
        Expr expr;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.name = node.name;
        expr.ordinal = node.methodOrdinal;

        for (auto& arg : node.args)
            expr.args.push_back(this->buildExpr(*arg));

        if (node.isStaticClassCall) {
            expr.className = node.staticClassName;
            expr.flag = true;
            expr.kind = ClassCall::getInstance().isRegistered(node.staticClassName)
                ? ExprKind::NativeCall
                : ExprKind::Call;
        } else {
            expr.kind = ExprKind::CallHost;
            expr.name = node.namespaces + "::" + node.name;
        }

        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildMacro(MacroNode& node) {
        Expr expr;
        expr.kind = ExprKind::CallMacro;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.name = node.name;

        for (auto& arg : node.args)
            expr.args.push_back(this->buildExpr(*arg));

        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildFuncCall(FuncCallNode& node) {
        std::vector<ExprId> args;
        args.reserve(node.args.size());
        for (auto& arg : node.args)
            args.push_back(this->buildExpr(*arg));

        Expr expr;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.name = node.name;
        expr.args = std::move(args);

        auto variantIt = this->program.enumVariants.find(node.name);
        if (variantIt != this->program.enumVariants.end()) {
            expr.kind = ExprKind::EnumConstruct;
            expr.tag = variantIt->second.second;
            expr.ordinal = variantIt->second.first;
            expr.arity = static_cast<int>(node.args.size());
            return this->addExpr(std::move(expr));
        }

        if (node.isFormReceiverCall && !this->declarativeReceivers.empty()) {
            expr.kind = ClassCall::getInstance().isRegistered(node.receiverClassName)
                ? ExprKind::NativeCall
                : ExprKind::MethodByName;
            expr.className = node.receiverClassName;
            expr.args.push_back(this->localRef(this->declarativeReceivers.back(), node.loc));
            return this->addExpr(std::move(expr));
        }

        if (node.isStaticCall) {
            expr.className = node.staticClassName;
            expr.ordinal = node.methodOrdinal;
            expr.flag = true;
            expr.kind = ClassCall::getInstance().isRegistered(node.staticClassName)
                ? ExprKind::NativeCall
                : ExprKind::Call;
            return this->addExpr(std::move(expr));
        }

        if (node.isCallable) {
            expr.kind = ExprKind::CallLambda;
            expr.name = node.resolvedName;
            return this->addExpr(std::move(expr));
        }

        expr.kind = ExprKind::Call;
        expr.name = node.resolvedName;

        auto ordinals = this->program.functionOrdinals.find(node.resolvedName);
        if (ordinals != this->program.functionOrdinals.end() &&
            node.functionOrdinal >= 0 &&
            static_cast<std::size_t>(node.functionOrdinal) < ordinals->second.size())
            expr.ordinal = ordinals->second[static_cast<std::size_t>(node.functionOrdinal)];

        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildMethodCall(MethodCallNode& node) {
        std::vector<ExprId> args;
        args.reserve(node.args.size());
        for (auto& arg : node.args)
            args.push_back(this->buildExpr(*arg));

        const ExprId receiver = this->buildExpr(*node.target);

        Expr expr;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.args = std::move(args);
        expr.bindsReceiver = !this->scopes.back().hasThis;

        if (node.dynamicDispatch) {
            expr.kind = ExprKind::MethodByName;
            expr.name = node.methodName;
            expr.args.push_back(receiver);
            return this->addExpr(std::move(expr));
        }

        if (node.isStaticCall) {
            expr.kind = ClassCall::getInstance().isRegistered(node.staticClassName)
                ? ExprKind::NativeCall
                : ExprKind::Call;
            expr.className = node.staticClassName;
            expr.name = node.methodName;
            expr.ordinal = node.methodOrdinal;
            expr.flag = true;
            return this->addExpr(std::move(expr));
        }

        expr.args.push_back(receiver);

        if (node.target->getType() == ASTNode::Type::Super &&
            node.methodOrdinal >= 0 &&
            this->classMethodIndices.contains(node.className)) {
            expr.kind = ExprKind::MethodCall;
            expr.className = node.className;
            expr.name = node.methodName;
            expr.ordinal = node.methodOrdinal;
            return this->addExpr(std::move(expr));
        }

        if (node.methodOrdinal >= 0 && this->classMethodIndices.contains(node.className)) {
            expr.kind = ExprKind::MethodVirtual;
            expr.className = node.className;
            expr.name = node.methodName;
            expr.ordinal = node.methodOrdinal;
            return this->addExpr(std::move(expr));
        }

        if (ClassCall::getInstance().isRegistered(node.className)) {
            expr.kind = ExprKind::NativeCall;
            expr.className = node.className;
            expr.name = node.methodName;
            return this->addExpr(std::move(expr));
        }

        if (node.methodName == "call") {
            expr.kind = ExprKind::MethodByName;
            expr.name = node.methodName;
            return this->addExpr(std::move(expr));
        }

        this->diagnostics.addError(node.loc, "Unresolved method call: " + node.methodName);
        return invalidId;
    }

    ExprId HirBuilder::buildNew(NewNode& node) {
        std::vector<ExprId> args;
        args.reserve(node.args.size());
        for (auto& arg : node.args)
            args.push_back(this->buildExpr(*arg));

        Expr expr;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.className = node.className;
        expr.args = std::move(args);

        auto classIt = this->program.classIndices.find(node.className);
        if (classIt != this->program.classIndices.end()) {
            expr.kind = ExprKind::New;
            expr.ordinal = classIt->second;
        } else if (ClassCall::getInstance().isRegistered(node.className)) {
            expr.kind = ExprKind::NewNative;
        } else {
            this->diagnostics.addError(node.loc, "Unknown class: " + node.className);
            return invalidId;
        }

        if (node.declarativeBlock) {
            std::string receiverName = node.receiverName;
            if (receiverName.empty())
                receiverName = ".__decl" + std::to_string(this->declarativeCounter++);

            auto local = this->resolveLocal(receiverName);
            LocalId receiver = invalidId;

            if (local) {
                receiver = *local;
            } else {
                expr.type = expr.type.valid() ? expr.type : this->lowerType(node.type);
                receiver = this->declareLocal(receiverName, expr.type, Ownership::Value);
            }

            expr.receiver = receiver;
            expr.body = this->buildDeclarativeBlock(*node.declarativeBlock, receiver);
        }

        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildLambda(LambdaNode& node) {
        const std::uint32_t bodyIndex = this->appendBody();

        Body& body = this->program.bodies[bodyIndex];
        Body* saved = this->current;
        this->current = &body;
        this->lastPartWasDeclaration = false;

        this->pushScope(false, false);
        this->captureStack.emplace_back();

        for (std::size_t index = 0; index < node.decl.params.size(); ++index) {
            ty::Ty type = this->program.types->dynamic();
            Ownership ownership = Ownership::Value;

            if (index < node.decl.paramTypes.size()) {
                type = this->lowerType(node.decl.paramTypes[index]);
                ownership = node.decl.paramTypes[index].ownership;
            } else if (node.decl.params[index].hasType) {
                type = lowerTypeExpr(*this->program.types, node.decl.params[index].typeExpr);
            }

            this->declareLocal(node.decl.params[index].name, type, ownership, true);
        }
        body.paramCount = static_cast<std::uint32_t>(node.decl.params.size());

        if (node.decl.body)
            body.root = this->buildBodyOf(*node.decl.body);

        body.returnsValue = !this->lastPartWasDeclaration;

        std::vector<Capture> captures = std::move(this->captureStack.back());
        this->captureStack.pop_back();
        this->popScope();
        this->current = saved;

        Expr expr;
        expr.kind = ExprKind::Lambda;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.bodyIndex = bodyIndex;
        expr.arity = static_cast<int>(node.decl.params.size());
        expr.captures = std::move(captures);
        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildArray(ArrayNode& node) {
        Expr expr;
        expr.kind = ExprKind::Array;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;

        for (auto& element : node.elements)
            expr.args.push_back(this->buildExpr(*element));

        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildArithmetic(ArithmeticNode& node) {
        const ExprId left = this->buildExpr(*node.left);
        const ExprId right = this->buildExpr(*node.right);

        if (node.overload) {
            Expr expr;
            expr.kind = ExprKind::MethodVirtual;
            expr.type = this->lowerType(node.type);
            expr.loc = node.loc;
            expr.className = node.overload->className;
            expr.name = node.overload->methodName;
            expr.ordinal = node.overload->methodOrdinal;
            expr.args.push_back(right);
            expr.args.push_back(left);
            return this->addExpr(std::move(expr));
        }

        Expr expr;
        expr.kind = ExprKind::Binary;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.op = node.op;
        expr.args.push_back(left);
        expr.args.push_back(right);
        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildUnary(UnaryNode& node) {
        const ExprId operand = this->buildExpr(*node.operand);

        if (node.overload) {
            Expr expr;
            expr.kind = ExprKind::MethodVirtual;
            expr.type = this->lowerType(node.type);
            expr.loc = node.loc;
            expr.className = node.overload->className;
            expr.name = node.overload->methodName;
            expr.ordinal = node.overload->methodOrdinal;
            expr.args.push_back(operand);
            return this->addExpr(std::move(expr));
        }

        if (node.op == "+" || node.op == "&")
            return operand;

        Expr expr;
        expr.kind = ExprKind::Unary;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.op = node.op;
        expr.args.push_back(operand);
        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildCompare(CompareNode& node) {
        const auto isNoneLiteral = [](const ExprNode& expr) -> bool {
            return expr.getType() == ASTNode::Type::Value &&
                std::holds_alternative<std::monostate>(static_cast<const ValueNode&>(expr).value);
        };

        if (node.op == "==" || node.op == "!=") {
            const bool leftNone = isNoneLiteral(*node.left);
            const bool rightNone = isNoneLiteral(*node.right);

            if (leftNone || rightNone) {
                ExprId tested = this->buildExpr(leftNone ? *node.right : *node.left);
                this->current->exprs[tested].flag2 = true;

                Expr expr;
                expr.kind = node.op == "==" ? ExprKind::HasValue : ExprKind::IsNone;
                expr.type = this->program.types->primitive(ty::TyKind::Bool);
                expr.loc = node.loc;
                expr.args.push_back(tested);
                return this->addExpr(std::move(expr));
            }
        }

        const ExprId left = this->buildExpr(*node.left);
        const ExprId right = this->buildExpr(*node.right);

        if (node.overload) {
            Expr expr;
            expr.kind = ExprKind::MethodVirtual;
            expr.type = this->lowerType(node.type);
            expr.loc = node.loc;
            expr.className = node.overload->className;
            expr.name = node.overload->methodName;
            expr.ordinal = node.overload->methodOrdinal;
            expr.args.push_back(right);
            expr.args.push_back(left);
            return this->addExpr(std::move(expr));
        }

        Expr expr;
        expr.kind = ExprKind::Compare;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.op = node.op;
        expr.args.push_back(left);
        expr.args.push_back(right);
        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildLogical(LogicalNode& node) {
        Expr expr;
        expr.kind = ExprKind::Logical;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.op = node.op;
        expr.args.push_back(this->buildExpr(*node.left));
        expr.args.push_back(this->buildExpr(*node.right));
        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildCoalesce(CoalesceNode& node) {
        Expr expr;
        expr.kind = ExprKind::Coalesce;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.args.push_back(this->buildExpr(*node.left));
        expr.args.push_back(this->buildExpr(*node.right));
        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildTry(TryNode& node) {
        Expr expr;
        expr.kind = ExprKind::Try;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.args.push_back(this->buildExpr(*node.operand));
        this->current->exprs[expr.args[0]].flag2 = true;
        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildRange(RangeNode& node) {
        Expr expr;
        expr.kind = ExprKind::Range;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.args.push_back(this->buildExpr(*node.start));
        expr.args.push_back(this->buildExpr(*node.end));
        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildIfExpr(IfNode& node) {
        Expr expr;
        expr.kind = ExprKind::If;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;

        const bool bindsPattern = node.condition &&
            node.condition->getType() == ASTNode::Type::LetPattern;

        if (bindsPattern) {
            expr.args.push_back(this->buildExpr(*node.condition));

            this->pushScope(false, true);
            auto& pattern = static_cast<LetPatternNode&>(*node.condition);
            Expr& condition = this->current->exprs[expr.args[0]];
            for (const std::string& binder : pattern.binders) {
                const LocalId local = this->declareLocal(binder, this->program.types->dynamic(), Ownership::Value);
                condition.binderLocals.push_back(local);
            }

            if (node.trueBranch)
                expr.body = this->buildBodyOf(*node.trueBranch);

            this->popScope();
        } else {
            expr.args.push_back(this->buildExpr(*node.condition));
            if (node.trueBranch)
                expr.body = this->buildBodyOf(*node.trueBranch);
        }

        if (node.falseBranch)
            expr.alternate = this->buildBodyOf(*node.falseBranch);

        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildMatchExpr(MatchNode& node) {
        Expr expr;
        expr.kind = ExprKind::Match;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.args.push_back(this->buildExpr(*node.target));

        const ty::Ty targetType = this->current->exprs[expr.args[0]].type;

        for (auto& armPtr : node.arms) {
            auto& arm = *armPtr;

            this->pushScope(false, true);

            MatchArm compiled;
            compiled.loc = arm.loc;
            compiled.literal = arm.literal;
            compiled.className = arm.className;
            compiled.binder = arm.binder;
            compiled.variantName = arm.variantName;
            compiled.binders = arm.binders;

            switch (arm.pattern) {
                case ::LOICollection::frontend::MatchArm::Pattern::Literal:
                    compiled.pattern = MatchArm::Pattern::Literal;
                    break;
                case ::LOICollection::frontend::MatchArm::Pattern::Type:
                    compiled.pattern = MatchArm::Pattern::Type;
                    break;
                case ::LOICollection::frontend::MatchArm::Pattern::Bind:
                    compiled.pattern = MatchArm::Pattern::Bind;
                    break;
                case ::LOICollection::frontend::MatchArm::Pattern::Wildcard:
                    compiled.pattern = MatchArm::Pattern::Wildcard;
                    break;
                case ::LOICollection::frontend::MatchArm::Pattern::EnumVariant:
                    compiled.pattern = MatchArm::Pattern::EnumVariant;
                    break;
            }

            if (!arm.binder.empty()) {
                ty::Ty type = targetType;
                if (!arm.className.empty())
                    type = this->program.types->adt(arm.className);
                compiled.binderLocals.push_back(this->declareLocal(arm.binder, type, Ownership::Value));
            }

            for (const std::string& binder : arm.binders)
                compiled.binderLocals.push_back(this->declareLocal(binder, this->program.types->dynamic(), Ownership::Value));

            if (arm.guard)
                compiled.guard = this->buildExpr(*arm.guard);

            if (arm.body)
                compiled.body = this->buildBodyOf(*arm.body);

            this->popScope();
            expr.arms.push_back(std::move(compiled));
        }

        return this->addExpr(std::move(expr));
    }

    ExprId HirBuilder::buildLetPattern(LetPatternNode& node) {
        Expr expr;
        expr.kind = ExprKind::LetPattern;
        expr.type = this->lowerType(node.type);
        expr.loc = node.loc;
        expr.name = node.variantName;
        expr.binders = node.binders;
        expr.flag = node.isEnumPattern;
        expr.args.push_back(node.value ? this->buildExpr(*node.value) : this->defaultExpr(node.loc));
        return this->addExpr(std::move(expr));
    }
}
