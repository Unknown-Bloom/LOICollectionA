#pragma once

#include <memory>
#include <string>
#include <vector>
#include <variant>
#include <unordered_map>

#include "LOICollectionA/frontend/ASTVisitor.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

namespace LOICollection::frontend {
    struct Object;
    using ObjectRef = std::shared_ptr<Object>;
    struct ArrayValue;
    using ArrayRef = std::shared_ptr<ArrayValue>;
    struct FunctionRef;
    using FunctionRefPtr = std::shared_ptr<FunctionRef>;

    enum class TypeKind {
        Unknown,
        Int,
        Float,
        String,
        Bool,
        Object,
        Function,
        Void,
        Array,
        Variant,
        Optional,
        Generic,
        None
    };

    struct TypeInfo {
        TypeKind kind = TypeKind::Unknown;
        std::string className{};
        std::vector<TypeInfo> variantOptions{};
        std::shared_ptr<TypeInfo> optionalInner{};
        std::string typeVar{};

        bool operator==(const TypeInfo& other) const {
            return kind == other.kind &&
                   className == other.className &&
                   typeVar == other.typeVar &&
                   variantOptions == other.variantOptions &&
                   ((!optionalInner && !other.optionalInner) ||
                    (optionalInner && other.optionalInner && *optionalInner == *other.optionalInner));
        }
    };

    struct TypeParam {
        std::string name;
        std::vector<std::string> bounds;
    };

    struct TypeExpr {
        SourceLocation loc;
        std::string name;
        std::vector<TypeExpr> args;
    };

    inline std::string typeInfoToString(const TypeInfo& type) {
        switch (type.kind) {
            case TypeKind::Unknown: return "unknown";
            case TypeKind::Int: return "int";
            case TypeKind::Float: return "float";
            case TypeKind::String: return "string";
            case TypeKind::Bool: return "bool";
            case TypeKind::Object: return "class " + type.className;
            case TypeKind::Function: return "function";
            case TypeKind::Void: return "void";
            case TypeKind::Array: return "array";
            case TypeKind::None: return "none";
            case TypeKind::Variant: {
                std::string result = "variant<";
                for (size_t i = 0; i < type.variantOptions.size(); ++i) {
                    if (i != 0)
                        result += ",";
                    result += typeInfoToString(type.variantOptions[i]);
                }
                result += ">";
                return result;
            }
            case TypeKind::Optional:
                return "optional<" +
                    (type.optionalInner ? typeInfoToString(*type.optionalInner) : std::string("unknown")) + ">";
            case TypeKind::Generic:
                return type.typeVar.empty() ? "generic" : type.typeVar;
        }

        return "unknown";
    };

    struct ASTNode {
        enum class Type {
            Value, Compare, Logical, If, Expr,
            Arithmetic, Unary, Function, Macro, Variable, Assignment,
            Class, Return, New, MemberAccess, MethodCall, This,
            Super, SuperCall, InstanceOf, FunctionDef, FuncCall, Lambda,
            Array, Index, Program, Block, Using,
            While, For, Break, Continue,
            CompoundAssign, ForIn, Range, Coalesce,
            Import, Component,
            Trait, Impl
        };
        [[nodiscard]] virtual Type getType() const = 0;
        
        virtual ~ASTNode() = default;

        virtual void accept(ASTVisitor& visitor) = 0;
    };

    struct ExprNode : ASTNode {
        TypeInfo type;
        bool preserveOptional = false;
        bool parenthesized = false;

        [[nodiscard]] Type getType() const override {
            return Type::Expr;
        }
    };

    struct ValueNode : ExprNode {
        using ValueType = std::variant<int, float, std::string, bool, ObjectRef, FunctionRefPtr, ArrayRef, std::monostate>;

        SourceLocation loc;
        ValueType value;
        
        explicit ValueNode(int v) : value(v) {}
        explicit ValueNode(float v) : value(v) {}
        explicit ValueNode(const std::string& v) : value(v) {}
        explicit ValueNode(bool v) : value(v) {}
        explicit ValueNode(std::monostate) : value(std::monostate{}) {}
        ValueNode(SourceLocation location, int v) : loc(location), value(v) {}
        ValueNode(SourceLocation location, float v) : loc(location), value(v) {}
        ValueNode(SourceLocation location, const std::string& v) : loc(location), value(v) {}
        ValueNode(SourceLocation location, bool v) : loc(location), value(v) {}
        ValueNode(SourceLocation location, std::monostate) : loc(location), value(std::monostate{}) {}

        [[nodiscard]] Type getType() const override {
            return Type::Value;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct VariableNode : ExprNode {
        SourceLocation loc;
        std::string name;
        bool isStaticField = false;
        std::string staticClassName;

        VariableNode(SourceLocation location, std::string n) : loc(location), name(std::move(n)) {}

        [[nodiscard]] Type getType() const override { return Type::Variable; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct AssignmentNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> target;
        std::unique_ptr<ExprNode> value;
        TypeExpr declaredType;
        bool hasDeclaredType = false;
        bool isDeclaration = false;

        AssignmentNode(SourceLocation location, auto&& t, auto&& val)
            : loc(location),
              target(std::forward<decltype(t)>(t)),
              value(std::forward<decltype(val)>(val)) {}

        [[nodiscard]] Type getType() const override { return Type::Assignment; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct CompoundAssignNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> target;
        std::unique_ptr<ExprNode> value;
        std::string op;

        CompoundAssignNode(SourceLocation location, auto&& t, auto&& val, std::string o)
            : loc(location),
              target(std::forward<decltype(t)>(t)),
              value(std::forward<decltype(val)>(val)),
              op(std::move(o)) {}

        [[nodiscard]] Type getType() const override { return Type::CompoundAssign; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct RangeNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> start;
        std::unique_ptr<ExprNode> end;

        RangeNode(SourceLocation location, auto&& s, auto&& e)
            : loc(location),
              start(std::forward<decltype(s)>(s)),
              end(std::forward<decltype(e)>(e)) {}

        [[nodiscard]] Type getType() const override { return Type::Range; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct CoalesceNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> left;
        std::unique_ptr<ExprNode> right;

        CoalesceNode(SourceLocation location, auto&& l, auto&& r)
            : loc(location),
              left(std::forward<decltype(l)>(l)),
              right(std::forward<decltype(r)>(r)) {}

        [[nodiscard]] Type getType() const override { return Type::Coalesce; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct IfNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> condition;
        std::unique_ptr<ASTNode> trueBranch;
        std::unique_ptr<ASTNode> falseBranch;
        
        IfNode(auto&& c, auto&& t, auto&& f)
            : condition(std::forward<decltype(c)>(c)), 
              trueBranch(std::forward<decltype(t)>(t)), 
              falseBranch(std::forward<decltype(f)>(f)) {}
        IfNode(SourceLocation location, auto&& c, auto&& t, auto&& f)
            : loc(location),
              condition(std::forward<decltype(c)>(c)),
              trueBranch(std::forward<decltype(t)>(t)),
              falseBranch(std::forward<decltype(f)>(f)) {}
        
        [[nodiscard]] Type getType() const override {
            return Type::If;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct WhileNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> condition;
        std::unique_ptr<ASTNode> body;

        WhileNode(SourceLocation location, auto&& c, auto&& b)
            : loc(location),
              condition(std::forward<decltype(c)>(c)),
              body(std::forward<decltype(b)>(b)) {}

        [[nodiscard]] Type getType() const override { return Type::While; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct ForNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> init;
        std::unique_ptr<ExprNode> condition;
        std::unique_ptr<ExprNode> step;
        std::unique_ptr<ASTNode> body;

        ForNode(SourceLocation location, auto&& i, auto&& c, auto&& s, auto&& b)
            : loc(location),
              init(std::forward<decltype(i)>(i)),
              condition(std::forward<decltype(c)>(c)),
              step(std::forward<decltype(s)>(s)),
              body(std::forward<decltype(b)>(b)) {}

        [[nodiscard]] Type getType() const override { return Type::For; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct ForInNode : ExprNode {
        SourceLocation loc;
        std::string indexVar;
        std::string elementVar;
        bool hasIndexVar = false;
        std::unique_ptr<ExprNode> iterable;
        std::unique_ptr<ASTNode> body;

        ForInNode(SourceLocation location, std::string element)
            : loc(location), elementVar(std::move(element)) {}

        [[nodiscard]] Type getType() const override { return Type::ForIn; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct BreakNode : ASTNode {
        SourceLocation loc;

        explicit BreakNode(SourceLocation location) : loc(location) {}

        [[nodiscard]] Type getType() const override { return Type::Break; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct ContinueNode : ASTNode {
        SourceLocation loc;

        explicit ContinueNode(SourceLocation location) : loc(location) {}

        [[nodiscard]] Type getType() const override { return Type::Continue; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct CompareNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> left;
        std::unique_ptr<ExprNode> right;
        std::string op;
        
        CompareNode(auto&& l, auto&& r, std::string o)
            : left(std::forward<decltype(l)>(l)),
              right(std::forward<decltype(r)>(r)),
              op(std::move(o)) {}
        CompareNode(SourceLocation location, auto&& l, auto&& r, std::string o)
            : loc(location),
              left(std::forward<decltype(l)>(l)),
              right(std::forward<decltype(r)>(r)),
              op(std::move(o)) {}
        
        [[nodiscard]] Type getType() const override {
            return Type::Compare;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct LogicalNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> left;
        std::unique_ptr<ExprNode> right;
        std::string op;
        
        LogicalNode(auto&& l, auto&& r, std::string o) 
            : left(std::forward<decltype(l)>(l)),
              right(std::forward<decltype(r)>(r)),
              op(std::move(o)) {}
        LogicalNode(SourceLocation location, auto&& l, auto&& r, std::string o)
            : loc(location),
              left(std::forward<decltype(l)>(l)),
              right(std::forward<decltype(r)>(r)),
              op(std::move(o)) {}
        
        [[nodiscard]] Type getType() const override {
            return Type::Logical;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct SequenceNode : ASTNode {
        std::vector<std::unique_ptr<ASTNode>> parts;

        void addPart(auto&& part) {
            parts.emplace_back(std::forward<decltype(part)>(part));
        }
    };

    struct ProgramNode : SequenceNode {
        [[nodiscard]] Type getType() const override {
            return Type::Program;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct BlockNode : SequenceNode {
        [[nodiscard]] Type getType() const override {
            return Type::Block;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct FunctionNode : ExprNode {
        SourceLocation loc;
        std::vector<std::unique_ptr<ExprNode>> args;
        std::string namespaces;
        std::string name;

        FunctionNode(auto&& a, std::string ns, std::string n)
            : args(std::forward<decltype(a)>(a)),
              namespaces(std::move(ns)),
              name(std::move(n)) {}
        FunctionNode(SourceLocation location, auto&& a, std::string ns, std::string n)
            : loc(location),
              args(std::forward<decltype(a)>(a)),
              namespaces(std::move(ns)),
              name(std::move(n)) {}
        
        [[nodiscard]] Type getType() const override {
            return Type::Function;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct MacroNode : ExprNode {
        SourceLocation loc;
        std::vector<std::unique_ptr<ExprNode>> args;
        std::string name;

        MacroNode(auto&& a, std::string n)
            : args(std::forward<decltype(a)>(a)),
              name(std::move(n)) {}
        MacroNode(SourceLocation location, auto&& a, std::string n)
            : loc(location),
              args(std::forward<decltype(a)>(a)),
              name(std::move(n)) {}
        
        [[nodiscard]] Type getType() const override {
            return Type::Macro;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct ArithmeticNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> left;
        std::unique_ptr<ExprNode> right;
        std::string op;
        
        ArithmeticNode(auto&& l, auto&& r, std::string o)
            : left(std::forward<decltype(l)>(l)),
              right(std::forward<decltype(r)>(r)),
              op(std::move(o)) {}
        ArithmeticNode(SourceLocation location, auto&& l, auto&& r, std::string o)
            : loc(location),
              left(std::forward<decltype(l)>(l)),
              right(std::forward<decltype(r)>(r)),
              op(std::move(o)) {}
        
        [[nodiscard]] Type getType() const override {
            return Type::Arithmetic;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct UnaryNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> operand;
        std::string op;
        
        UnaryNode(auto&& expr, std::string o)
            : operand(std::forward<decltype(expr)>(expr)),
              op(std::move(o)) {}
        UnaryNode(SourceLocation location, auto&& expr, std::string o)
            : loc(location),
              operand(std::forward<decltype(expr)>(expr)),
              op(std::move(o)) {}
        
        [[nodiscard]] Type getType() const override {
            return Type::Unary;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct ClassMember {
        SourceLocation loc;
        std::string name;
        bool isPrivate = false;
        bool isStatic = false;
        std::unique_ptr<ExprNode> defaultExpr;
        TypeInfo type;
        TypeExpr typeExpr;
        bool hasTypeExpr = false;
        bool hasDefault = false;
    };

    struct MethodParam {
        std::string name;
        TypeExpr typeExpr;
        bool hasType = false;
    };

    struct MethodDecl {
        SourceLocation loc;
        std::string name;
        std::vector<MethodParam> params;
        TypeExpr returnTypeExpr;
        bool hasReturnType = false;
        bool isConstructor = false;
        bool isPrivate = false;
        bool isStatic = false;
        std::unique_ptr<ASTNode> body;

        std::vector<TypeInfo> paramTypes;
        TypeInfo returnType;
        bool hasReturnStatement = false;
        bool hasSuperCall = false;
        std::vector<TypeParam> typeParams;
    };

    struct TraitNode : ASTNode {
        SourceLocation loc;
        std::string name;
        std::vector<MethodDecl> methods;

        TraitNode(SourceLocation location, std::string n)
            : loc(location), name(std::move(n)) {}

        [[nodiscard]] Type getType() const override { return Type::Trait; }

        void accept(ASTVisitor&) override {}
    };

    struct ImplNode : ASTNode {
        SourceLocation loc;
        std::optional<TypeExpr> trait;
        TypeExpr target;
        std::vector<MethodDecl> methods;
        std::vector<ClassMember> consts;

        explicit ImplNode(SourceLocation location) : loc(location) {}

        [[nodiscard]] Type getType() const override { return Type::Impl; }

        void accept(ASTVisitor&) override {}
    };

    struct ClassNode : ASTNode {
        SourceLocation loc;
        std::string name;
        std::string baseClassName;
        std::vector<ClassMember> members;
        std::vector<MethodDecl> methods;
        int constructorIndex = -1;
        std::vector<TypeParam> typeParams;

        ClassNode(SourceLocation location, std::string n)
            : loc(location), name(std::move(n)) {}

        [[nodiscard]] Type getType() const override { return Type::Class; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct ReturnNode : ASTNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> value;

        ReturnNode(SourceLocation location, auto&& val)
            : loc(location), value(std::forward<decltype(val)>(val)) {}

        [[nodiscard]] Type getType() const override { return Type::Return; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct NewNode : ExprNode {
        SourceLocation loc;
        std::string className;
        std::vector<std::unique_ptr<ExprNode>> args;
        std::unique_ptr<BlockNode> declarativeBlock;
        std::string receiverName;

        NewNode(SourceLocation location, std::string name, auto&& a)
            : loc(location), className(std::move(name)), args(std::forward<decltype(a)>(a)) {}

        [[nodiscard]] Type getType() const override { return Type::New; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct MemberAccessNode : ExprNode {
        enum class MemberKind {
            Normal,
            TypeOf,
            Value,
            HasValue
        };

        SourceLocation loc;
        std::unique_ptr<ExprNode> target;
        std::string memberName;
        bool isStaticAccess = false;
        std::string staticClassName;
        bool isSafe = false;
        MemberKind memberKind = MemberKind::Normal;

        MemberAccessNode(SourceLocation location, auto&& t, std::string member)
            : loc(location), target(std::forward<decltype(t)>(t)), memberName(std::move(member)) {}

        [[nodiscard]] Type getType() const override { return Type::MemberAccess; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct MethodCallNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> target;
        std::string methodName;
        std::vector<std::unique_ptr<ExprNode>> args;

        std::string className;
        int methodOrdinal = -1;
        bool dynamicDispatch = false;
        bool isStaticCall = false;
        std::string staticClassName;

        MethodCallNode(SourceLocation location, auto&& t, std::string name, auto&& a)
            : loc(location),
              target(std::forward<decltype(t)>(t)),
              methodName(std::move(name)),
              args(std::forward<decltype(a)>(a)) {}

        [[nodiscard]] Type getType() const override { return Type::MethodCall; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct UsingNode : ASTNode {
        SourceLocation loc;
        std::string name;
        TypeExpr type;

        UsingNode(SourceLocation location, std::string n)
            : loc(location), name(std::move(n)) {}

        [[nodiscard]] Type getType() const override { return Type::Using; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct ArrayNode : ExprNode {
        SourceLocation loc;
        std::vector<std::unique_ptr<ExprNode>> elements;

        ArrayNode(SourceLocation location, auto&& elems)
            : loc(location), elements(std::forward<decltype(elems)>(elems)) {}

        [[nodiscard]] Type getType() const override {
            return Type::Array;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct IndexAccessNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> target;
        std::unique_ptr<ExprNode> index;
        bool isSafe = false;

        IndexAccessNode(SourceLocation location, auto&& t, auto&& i)
            : loc(location),
              target(std::forward<decltype(t)>(t)),
              index(std::forward<decltype(i)>(i)) {}

        [[nodiscard]] Type getType() const override {
            return Type::Index;
        }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct ThisNode : ExprNode {
        SourceLocation loc;

        explicit ThisNode(SourceLocation location) : loc(location) {}

        [[nodiscard]] Type getType() const override { return Type::This; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct SuperNode : ExprNode {
        SourceLocation loc;

        explicit SuperNode(SourceLocation location) : loc(location) {}

        [[nodiscard]] Type getType() const override { return Type::Super; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct SuperCallNode : ExprNode {
        SourceLocation loc;
        std::vector<std::unique_ptr<ExprNode>> args;

        std::string className;
        int constructorIndex = -1;

        SuperCallNode(SourceLocation location, auto&& a)
            : loc(location), args(std::forward<decltype(a)>(a)) {}

        [[nodiscard]] Type getType() const override { return Type::SuperCall; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct InstanceOfNode : ExprNode {
        SourceLocation loc;
        std::unique_ptr<ExprNode> target;
        std::string className;

        InstanceOfNode(SourceLocation location, auto&& t, std::string name)
            : loc(location), target(std::forward<decltype(t)>(t)), className(std::move(name)) {}

        [[nodiscard]] Type getType() const override { return Type::InstanceOf; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct FunctionDefNode : ASTNode {
        SourceLocation loc;
        std::string name;
        MethodDecl decl;

        FunctionDefNode(SourceLocation location, std::string n)
            : loc(location), name(std::move(n)) {}

        [[nodiscard]] Type getType() const override { return Type::FunctionDef; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct LambdaNode : ExprNode {
        SourceLocation loc;
        MethodDecl decl;

        explicit LambdaNode(SourceLocation location) : loc(location) {}

        [[nodiscard]] Type getType() const override { return Type::Lambda; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct FuncCallNode : ExprNode {
        SourceLocation loc;
        std::string name;
        std::vector<std::unique_ptr<ExprNode>> args;

        std::string resolvedName;
        int functionOrdinal = -1;
        bool isCallable = false;
        bool isStaticCall = false;
        int methodOrdinal = -1;
        std::string staticClassName;

        bool isFormReceiverCall = false;
        std::string receiverClassName;

        FuncCallNode(SourceLocation location, std::string n, auto&& a)
            : loc(location), name(std::move(n)), args(std::forward<decltype(a)>(a)) {}

        [[nodiscard]] Type getType() const override { return Type::FuncCall; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct ImportNode : ASTNode {
        SourceLocation loc;
        std::string path;

        ImportNode(SourceLocation location, std::string p)
            : loc(location), path(std::move(p)) {}

        [[nodiscard]] Type getType() const override { return Type::Import; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct ComponentNode : ASTNode {
        SourceLocation loc;
        std::string name;
        std::vector<std::string> params;
        std::unique_ptr<BlockNode> body;

        ComponentNode(SourceLocation location, std::string n)
            : loc(location), name(std::move(n)) {}

        [[nodiscard]] Type getType() const override { return Type::Component; }

        void accept(ASTVisitor& visitor) override {
            visitor.visit(*this);
        }
    };

    struct NativeHandle {
        virtual ~NativeHandle() = default;

        virtual void release() {}
    };

    struct FieldLayout {
        std::vector<std::string> names;
        std::unordered_map<std::string, int> slots;

        explicit FieldLayout(std::vector<std::string> fieldNames)
            : names(std::move(fieldNames)) {
            for (size_t i = 0; i < this->names.size(); ++i)
                this->slots.emplace(this->names[i], static_cast<int>(i));
        }

        [[nodiscard]] int slotOf(const std::string& name) const {
            auto it = this->slots.find(name);
            return it == this->slots.end() ? -1 : it->second;
        }
    };

    using FieldLayoutPtr = std::shared_ptr<const FieldLayout>;
    using SpillMap = std::unordered_map<std::string, ValueNode::ValueType>;

    struct Object {
        std::string className;
        int classIndex = -1;
        FieldLayoutPtr layout;
        std::vector<ValueNode::ValueType> slots;
        std::unique_ptr<SpillMap> spill;

        std::shared_ptr<NativeHandle> native;

        void resize(size_t count) {
            this->slots.resize(count);
        }

        void adoptLayout();

        void assign(const std::string& name, ValueNode::ValueType value) {
            if (!this->layout)
                this->adoptLayout();

            if (ValueNode::ValueType* slot = this->findDeclared(name)) {
                *slot = std::move(value);
                return;
            }

            this->spillSlot(name) = std::move(value);
        }

        [[nodiscard]] ValueNode::ValueType* find(const std::string& name) {
            if (!this->layout)
                this->adoptLayout();

            if (ValueNode::ValueType* slot = this->findDeclared(name))
                return slot;

            if (!this->spill)
                return nullptr;

            auto it = this->spill->find(name);
            return it == this->spill->end() ? nullptr : &it->second;
        }

        [[nodiscard]] const ValueNode::ValueType* find(const std::string& name) const {
            if (const ValueNode::ValueType* slot = this->findDeclared(name))
                return slot;

            if (!this->spill)
                return nullptr;

            auto it = this->spill->find(name);
            return it == this->spill->end() ? nullptr : &it->second;
        }

        [[nodiscard]] ValueNode::ValueType& at(const std::string& name) {
            if (ValueNode::ValueType* slot = this->find(name))
                return *slot;

            return this->spillSlot(name);
        }

    private:
        [[nodiscard]] ValueNode::ValueType* findDeclared(const std::string& name) {
            int index = this->layout ? this->layout->slotOf(name) : -1;
            return index < 0 || static_cast<size_t>(index) >= this->slots.size()
                ? nullptr
                : &this->slots[static_cast<size_t>(index)];
        }

        [[nodiscard]] const ValueNode::ValueType* findDeclared(const std::string& name) const {
            int index = this->layout ? this->layout->slotOf(name) : -1;
            return index < 0 || static_cast<size_t>(index) >= this->slots.size()
                ? nullptr
                : &this->slots[static_cast<size_t>(index)];
        }

        ValueNode::ValueType& spillSlot(const std::string& name) {
            if (!this->spill)
                this->spill = std::make_unique<SpillMap>();

            return (*this->spill)[name];
        }
    };

    struct ArrayValue {
        std::vector<ValueNode::ValueType> elements;
    };

    namespace ir {
        struct BytecodeChunk;
    }

    using GlobalsTable = std::unordered_map<std::string, ValueNode::ValueType>;

    struct FunctionRef {
        std::shared_ptr<const ir::BytecodeChunk> owner;

        int bodyIndex = -1;
        int argCount = 0;

        bool hasThis = false;
        std::weak_ptr<Object> thisObj;

        std::vector<ValueNode::ValueType> captures;
        std::weak_ptr<GlobalsTable> globals;
    };
}
