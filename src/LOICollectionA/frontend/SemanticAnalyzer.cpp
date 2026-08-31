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
        this->impls.clear();
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
                case ASTNode::Type::Impl:
                    registerImpl(static_cast<ImplNode&>(*part));
                    break;
                default:
                    break;
            }
        }

        processImpls();

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

}
