#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <unordered_set>
#include <unordered_map>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Iteration.h"

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::frontend {
    class SemanticAnalyzer {
    public:
        LOICOLLECTION_A_API SemanticAnalyzer(DiagnosticEngine& diag);

        LOICOLLECTION_A_API void analyze(ProgramNode& root);

    private:
        struct MethodScope {
            std::optional<std::reference_wrapper<ClassNode>> cls;
            std::optional<std::reference_wrapper<MethodDecl>> method;

            int loopDepth = 0;

            [[nodiscard]] bool hasClass() const { return cls.has_value(); }
            [[nodiscard]] bool hasMethod() const { return method.has_value(); }
            [[nodiscard]] ClassNode& classRef() const { return cls->get(); }
            [[nodiscard]] MethodDecl& methodRef() const { return method->get(); }
        };

        struct FieldRef {
            std::reference_wrapper<ClassMember> member;
            std::reference_wrapper<ClassNode> owner;
        };

        struct ConstructorRef {
            std::reference_wrapper<MethodDecl> method;
            std::reference_wrapper<ClassNode> owner;
        };

        using TypeSubst = std::unordered_map<std::string, TypeInfo>;

        struct StaticMethodRef {
            std::reference_wrapper<MethodDecl> method;
            std::reference_wrapper<ClassNode> owner;
            TypeSubst subst;
        };

        struct ReceiverBinding {
            std::string name;
            std::string className;
        };

        struct ReceiverCapture {
            std::string name;
            SourceLocation loc;
        };

        DiagnosticEngine& diagnostics;

        std::vector<std::unordered_map<std::string, TypeInfo>> blockScopes;
        std::vector<std::string> formReceivers;
        std::vector<ReceiverBinding> receiverBindings;
        std::vector<ReceiverCapture> receiverCaptures;
        int closureDepth = 0;

        std::vector<std::reference_wrapper<ClassNode>> classes;
        std::vector<std::reference_wrapper<ClassNode>> orderedClasses;
        std::unordered_map<std::string, std::reference_wrapper<ClassNode>> classByName;
        std::unordered_map<std::string, std::vector<std::string>> classMethodOrder;
        std::unordered_map<std::string, std::unordered_map<std::string, int>> classMethodOrdinals;
        std::unordered_map<std::string, std::vector<std::string>> classStaticMethodOrder;
        std::unordered_map<std::string, std::unordered_map<std::string, int>> classStaticMethodOrdinals;
        std::vector<std::reference_wrapper<ImplNode>> impls;
        std::vector<std::reference_wrapper<FunctionDefNode>> functions;
        struct EnumVariantInfo {
            int enumIndex = -1;
            int tag = -1;
            int arity = 0;
            std::string enumName;
            std::vector<TypeExpr> fieldTypes;
            std::vector<TypeInfo> payloadTypes;
        };
        std::unordered_map<std::string, int> enumByName;
        std::unordered_map<std::string, EnumVariantInfo> enumVariant;
        std::unordered_map<std::string, std::reference_wrapper<EnumDeclNode>> enumNodes;
        std::unordered_map<std::string, std::reference_wrapper<UsingNode>> aliasNodes;
        std::unordered_map<std::string, TypeInfo> classBaseTypes;
        std::unordered_map<std::string, std::vector<std::reference_wrapper<FunctionDefNode>>> functionsByName;
        std::unordered_map<std::string, TypeInfo> globalTypes;
        std::unordered_map<std::string, TypeInfo> declaredGlobals;
        std::unordered_map<std::string, TypeExpr> aliasExprs;
        std::unordered_map<std::string, SourceLocation> aliasLocs;
        std::unordered_map<std::string, TypeInfo> typeAliases;
        std::unordered_set<std::string> resolvingAliases;
        std::unordered_map<std::string, std::unordered_set<std::string>> constructorAssignedMembers;

        struct TraitMethod {
            std::string name;
            size_t paramCount = 0;
            bool hasReturnType = false;
            TypeExpr returnTypeExpr;
        };

        std::unordered_map<std::string, std::vector<TraitMethod>> traits;
        std::unordered_map<std::string, TypeInfo> activeTypeParams;
        std::unordered_map<std::string, std::vector<std::string>> activeTypeParamBounds;

        [[nodiscard]] std::optional<std::reference_wrapper<ClassNode>> findClass(const std::string& name) const;
        [[nodiscard]] ClassLookup classLookup() const;

        [[nodiscard]] TypeInfo typeOfValue(const ValueNode::ValueType& value) const;
        [[nodiscard]] TypeInfo typeFromName(const std::string& name, SourceLocation loc, bool reportError) const;
        [[nodiscard]] std::string typeToString(const TypeInfo& type) const;
        [[nodiscard]] bool isNumeric(const TypeInfo& type) const;
        [[nodiscard]] bool isNameDefined(const std::string& name, MethodScope& scope) const;
        [[nodiscard]] bool isAssignableTo(const TypeInfo& target, const TypeInfo& from) const;

        [[nodiscard]] TypeInfo selfType(const ClassNode& cls) const;
        [[nodiscard]] TypeSubst substitutionFor(const ClassNode& owner, const TypeInfo& receiver) const;
        [[nodiscard]] bool unifyType(const TypeInfo& pattern, const TypeInfo& actual, TypeSubst& subst) const;
        [[nodiscard]] bool inferTypeArgs(
            const std::vector<TypeParam>& typeParams,
            const std::vector<TypeInfo>& patterns,
            const std::vector<TypeInfo>& actuals,
            TypeSubst& subst) const;
        [[nodiscard]] bool typeParamBoundsSatisfied(
            const std::vector<TypeParam>& typeParams,
            const TypeSubst& subst) const;
        [[nodiscard]] bool checkTypeParamBounds(
            const std::vector<TypeParam>& typeParams,
            const TypeSubst& subst,
            SourceLocation loc,
            const std::string& what) const;
        [[nodiscard]] bool typeParamBoundSatisfied(const std::string& param, const std::string& traitName) const;
        [[nodiscard]] bool typeArgumentsCompatible(const TypeInfo& target, const TypeInfo& from) const;
        [[nodiscard]] TypeInfo resolveClassType(
            ClassNode& cls, const std::vector<TypeExpr>& args, SourceLocation loc, bool reportError);
        [[nodiscard]] TypeInfo resolveEnumType(
            EnumDeclNode& node, const std::vector<TypeExpr>& args, SourceLocation loc, bool reportError);
        void validateTypeParamBounds(const std::vector<TypeParam>& typeParams, SourceLocation loc);
        void pushTypeParams(const std::vector<TypeParam>& typeParams);
        [[nodiscard]] TypeInfo enumTargetType(const EnumVariantInfo& info, const TypeInfo& value) const;
        [[nodiscard]] std::string receiverVariable(MethodCallNode& node) const;
        void reportReceiverCaptures(const std::string& name);
        void requireLet(AssignmentNode& node, const VariableNode& var);
        bool assignTypedBinding(TypeInfo& target, const TypeInfo& from, SourceLocation loc, const std::string& name);

        void registerClass(ClassNode& node);
        void registerEnum(EnumDeclNode& node);
        void collectTypeAliases(ProgramNode& root);
        void seedOperatorTraits();
        void resolveDeclaredTypes();
        void resolveHierarchy();
        void buildMethodOrdinals();
        void validateConstructors();
        void validateMemberInitialization();
        void registerFunction(FunctionDefNode& node);
        void registerTrait(TraitNode& node);
        void registerImpl(ImplNode& node);
        void processImpls();
        void checkTopLevel(ProgramNode& root);
        void checkClassBodies();
        void checkFunctionBodies();
        void checkClassBody(ClassNode& cls);
        void checkBody(std::optional<std::reference_wrapper<ClassNode>> cls, MethodDecl& method);

        void checkStatement(ASTNode& node, MethodScope& scope);
        TypeInfo checkExpr(ExprNode& node, MethodScope& scope);
        TypeInfo checkExprImpl(ExprNode& node, MethodScope& scope);
        TypeInfo checkAssignment(AssignmentNode& node, MethodScope& scope);
        TypeInfo checkMemberAccess(MemberAccessNode& node, MethodScope& scope);
        TypeInfo checkMemberAccessImpl(MemberAccessNode& node, MethodScope& scope, TypeInfo targetType);
        TypeInfo checkMethodCall(MethodCallNode& node, MethodScope& scope);
        TypeInfo checkFuncCall(FuncCallNode& node, MethodScope& scope);
        TypeInfo checkNew(NewNode& node, MethodScope& scope);
        void checkDeclarativeBlock(NewNode& node, MethodScope& scope);
        TypeInfo checkSuperCall(SuperCallNode& node, MethodScope& scope);
        TypeInfo checkInstanceOf(InstanceOfNode& node, MethodScope& scope);
        TypeInfo checkMatch(MatchNode& node, MethodScope& scope);
        TypeInfo checkLetPattern(LetPatternNode& node, MethodScope& scope);
        void bindLetPatternTypes(LetPatternNode& node);
        TypeInfo checkReturn(ReturnNode& node, MethodScope& scope);
        TypeInfo checkLambda(LambdaNode& node, MethodScope& scope);

        std::optional<OperatorOverload> resolveOperatorOverload(const std::string& op, const TypeInfo& receiver);

        TypeInfo lookupName(const std::string& name, MethodScope& scope);
        void unify(TypeInfo& target, const TypeInfo& from, SourceLocation loc, const std::string& what);
        TypeInfo resolveTypeExpr(const TypeExpr& expr, SourceLocation loc, bool reportError);

        [[nodiscard]] TypeInfo substituteType(
            const TypeInfo& type, const std::unordered_map<std::string, TypeInfo>& subst) const;
        [[nodiscard]] bool satisfiesTrait(const TypeInfo& type, const std::string& traitName) const;

        [[nodiscard]] bool declaresCallableMethod(const TypeInfo& type) const;

        [[nodiscard]] size_t knownParamCount(const MethodDecl& method) const;
        [[nodiscard]] std::string methodSignature(const MethodDecl& method) const;
        [[nodiscard]] int methodOrdinal(const std::string& className, const std::string& signature) const;
        [[nodiscard]] bool isDerived(const std::string& derivedName, const std::string& baseName) const;
        [[nodiscard]] bool isTypeCompatible(const TypeInfo& target, const TypeInfo& from) const;

        [[nodiscard]] std::optional<FieldRef> findField(ClassNode& cls, const std::string& name) const;
        [[nodiscard]] std::optional<FieldRef> findStaticField(ClassNode& cls, const std::string& name) const;
        [[nodiscard]] std::optional<ConstructorRef> findConstructor(ClassNode& cls) const;
        [[nodiscard]] std::optional<StaticMethodRef> findStaticMethod(
            ClassNode& cls, const std::string& name, const std::vector<TypeInfo>& argTypes,
            const MethodScope& scope, const TypeInfo& classType = {},
            const std::vector<TypeInfo>& methodTypeArgs = {}
        ) const;
        [[nodiscard]] int staticMethodOrdinal(const std::string& className, const std::string& signature) const;
    };
}
