#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/frontend/ir/ty/TypeContext.h"

namespace LOICollection::frontend::ir::hir {
    inline constexpr std::uint32_t invalidId = 0xFFFFFFFFu;

    using LocalId = std::uint32_t;
    using ExprId = std::uint32_t;
    using StmtId = std::uint32_t;

    struct Local {
        std::string name;
        ty::Ty type{};
        Ownership ownership = Ownership::Value;
        bool parameter = false;
        bool captured = false;
    };

    enum class ExprKind : std::uint8_t {
        Invalid,
        Literal,
        LocalRef,
        GlobalRef,
        This,
        Super,
        Field,
        SafeField,
        Index,
        SafeIndex,
        Call,
        CallHost,
        CallMacro,
        CallLambda,
        MethodCall,
        MethodVirtual,
        MethodByName,
        NativeCall,
        SuperCall,
        New,
        NewNative,
        Lambda,
        Array,
        Unary,
        Binary,
        Compare,
        Logical,
        TypeOf,
        HasValue,
        IsNone,
        Unwrap,
        Len,
        InstanceOf,
        EnumConstruct,
        EnumPayload,
        Coalesce,
        Try,
        Range,
        Assign,
        CompoundAssign,
        If,
        Match,
        LetPattern
    };

    struct MatchArm {
        enum class Pattern : std::uint8_t {
            Literal,
            Type,
            Bind,
            Wildcard,
            EnumVariant
        };

        Pattern pattern = Pattern::Wildcard;
        SourceLocation loc{};
        ValueNode::ValueType literal{};
        std::string className;
        std::string binder;
        std::string variantName;
        std::vector<std::string> binders;
        std::vector<LocalId> binderLocals;
        ExprId guard = invalidId;
        std::vector<StmtId> body;
    };

    struct Capture {
        std::string name;
        LocalId source = invalidId;
        LocalId target = invalidId;
    };

    struct Expr {
        ExprKind kind = ExprKind::Invalid;
        ty::Ty type{};
        SourceLocation loc{};
        std::vector<ExprId> args;
        std::vector<StmtId> body;
        std::vector<StmtId> alternate;
        std::vector<MatchArm> arms;
        std::vector<Capture> captures;
        std::vector<std::string> binders;
        std::vector<LocalId> binderLocals;
        std::string name;
        std::string className;
        std::string op;
        ValueNode::ValueType literal{};
        int ordinal = -1;
        int slot = -1;
        int tag = -1;
        int arity = 0;
        std::uint32_t bodyIndex = invalidId;
        LocalId receiver = invalidId;
        bool flag = false;
        bool flag2 = false;
        bool bindsReceiver = false;
    };

    enum class StmtKind : std::uint8_t {
        Let,
        Assign,
        CompoundAssign,
        Expr,
        Block,
        If,
        While,
        For,
        ForIn,
        Match,
        Return,
        Break,
        Continue,
        Panic
    };

    struct Stmt {
        StmtKind kind = StmtKind::Expr;
        SourceLocation loc{};
        ExprId value = invalidId;
        ExprId target = invalidId;
        ExprId condition = invalidId;
        std::vector<StmtId> body;
        std::vector<StmtId> alternate;
        std::vector<MatchArm> arms;
        std::string name;
        std::string op;
        std::string className;
        LocalId elementLocal = invalidId;
        LocalId indexLocal = invalidId;
        int shape = 0;
        bool flag = false;
        bool flag2 = false;
    };

    struct Body {
        std::vector<Local> locals;
        std::vector<Expr> exprs;
        std::vector<Stmt> stmts;
        std::vector<StmtId> root;
        std::uint32_t paramCount = 0;
        bool isTopLevel = false;
        bool returnsValue = false;

        [[nodiscard]] bool validLocal(LocalId id) const { return id < this->locals.size(); }
        [[nodiscard]] bool validExpr(ExprId id) const { return id < this->exprs.size(); }
        [[nodiscard]] bool validStmt(StmtId id) const { return id < this->stmts.size(); }
    };

    struct Param {
        std::string name;
        ty::Ty type{};
        Ownership ownership = Ownership::Value;
    };

    struct Function {
        std::string name;
        std::vector<Param> params;
        ty::Ty result{};
        std::uint32_t bodyIndex = invalidId;
        int ordinal = -1;
    };

    struct Method {
        std::string name;
        std::string signature;
        std::vector<Param> params;
        ty::Ty result{};
        std::uint32_t bodyIndex = invalidId;
        int ordinal = -1;
        int classIndex = -1;
        bool isConstructor = false;
        bool isStatic = false;
    };

    struct Class {
        std::string name;
        std::string baseName;
        ty::Ty type{};
        std::vector<ty::FieldDef> fields;
        std::vector<ty::FieldDef> staticFields;
        std::vector<Method> methods;
        std::vector<int> methodTable;
        std::vector<std::string> methodSignatures;
        std::vector<int> staticMethodTable;
        std::vector<std::string> staticMethodSignatures;
        std::vector<int> ancestors;
        std::uint64_t ancestorMask = 0;
        int classIndex = -1;
        int constructorOrdinal = -1;
    };

    struct EnumVariant {
        std::string name;
        std::vector<ty::Ty> payload;
    };

    struct Enum {
        std::string name;
        std::vector<EnumVariant> variants;
        int enumIndex = -1;
    };

    struct Program {
        std::shared_ptr<ty::TypeContext> types;
        Body topLevel;
        std::deque<Body> bodies;
        std::vector<Function> functions;
        std::vector<Class> classes;
        std::vector<Enum> enums;
        std::unordered_map<std::string, std::vector<int>> functionOrdinals;
        std::unordered_map<std::string, int> classIndices;
        std::unordered_map<std::string, int> enumIndices;
        std::unordered_map<std::string, std::pair<int, int>> enumVariants;
    };
}
