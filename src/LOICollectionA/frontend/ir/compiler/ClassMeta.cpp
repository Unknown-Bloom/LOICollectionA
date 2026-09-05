#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "LOICollectionA/frontend/ir/ByteCode.h"
#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/ir/Compiler.h"

namespace LOICollection::frontend::ir {

    void Compiler::registerClassMeta(ClassNode& node) {
        if (classIndices.contains(node.name))
            return;

        int baseIdx = -1;
        if (!node.baseClassName.empty()) {
            if (this->registeringClasses.contains(node.name)) {
                this->diagnostics.addError(node.loc,
                    "Circular inheritance involving class '" + node.name + "'");
                return;
            }

            this->registeringClasses.insert(node.name);

            auto baseIt = this->classNodes.find(node.baseClassName);
            if (baseIt == this->classNodes.end()) {
                this->diagnostics.addError(node.loc, "Unknown base class: " + node.baseClassName);
                this->registeringClasses.erase(node.name);
                return;
            }

            if (!this->classIndices.contains(node.baseClassName))
                this->registerClassMeta(baseIt->second.get());

            auto registeredIt = this->classIndices.find(node.baseClassName);
            if (registeredIt == this->classIndices.end()) {
                this->diagnostics.addError(node.loc, "Failed to register base class: " + node.baseClassName);
                this->registeringClasses.erase(node.name);
                return;
            }

            baseIdx = registeredIt->second;
            this->registeringClasses.erase(node.name);
        }

        int classIdx = static_cast<int>(this->chunk.classes.size());
        this->classIndices[node.name] = classIdx;
        this->registeredClasses.insert(classIdx);

        ir::ClassMeta meta;
        meta.name = node.name;
        meta.baseClassIndex = baseIdx;

        if (baseIdx >= 0) {
            const auto& base = this->chunk.classes[baseIdx];
            meta.fieldNames = base.fieldNames;
            meta.defaults = base.defaults;
            meta.hasDefault = base.hasDefault;
            meta.constructorIndex = base.constructorIndex;
            meta.methods = base.methods;
            meta.methodSignatures = base.methodSignatures;
            meta.staticMethods = base.staticMethods;
            meta.staticMethodSignatures = base.staticMethodSignatures;
            meta.staticFieldNames = base.staticFieldNames;
            meta.staticDefaults = base.staticDefaults;
            meta.staticHasDefault = base.staticHasDefault;
            meta.ancestorIndices.push_back(baseIdx);
            meta.ancestorIndices.insert(meta.ancestorIndices.end(), base.ancestorIndices.begin(), base.ancestorIndices.end());
        }

        for (const auto& member : node.members) {
            auto& names = member.isStatic ? meta.staticFieldNames : meta.fieldNames;
            auto& defaults = member.isStatic ? meta.staticDefaults : meta.defaults;
            auto& hasDefault = member.isStatic ? meta.staticHasDefault : meta.hasDefault;

            auto fieldIt = std::ranges::find(names, member.name);
            if (fieldIt == names.end()) {
                names.push_back(member.name);
                hasDefault.push_back(member.hasDefault);
                defaults.emplace_back();
                fieldIt = std::ranges::find(names, member.name);
            } else {
                auto fieldIdx = static_cast<size_t>(std::distance(names.begin(), fieldIt));
                hasDefault[fieldIdx] = member.hasDefault;
            }

            auto fieldIdx = static_cast<size_t>(std::distance(names.begin(), fieldIt));
            if (member.hasDefault) {
                if (member.defaultExpr) {
                    auto value = this->constantValue(*member.defaultExpr);
                    if (value.has_value()) {
                        defaults[fieldIdx] = *value;
                        continue;
                    }
                }

                this->diagnostics.addError(node.loc,
                    "Member default value of '" + member.name + "' must be a constant literal");
                defaults[fieldIdx] = ValueNode::ValueType{};
            } else {
                defaults[fieldIdx] = ValueNode::ValueType{};
            }
        }

        for (auto & method : node.methods) {
            int methodIdx = static_cast<int>(methodCount++);

            if (method.isConstructor)
                meta.constructorIndex = methodIdx;
            else {
                std::string signature = this->methodSignature(method);
                auto& signatures = method.isStatic ? meta.staticMethodSignatures : meta.methodSignatures;
                auto& methods = method.isStatic ? meta.staticMethods : meta.methods;

                auto sigIt = std::ranges::find(signatures, signature);
                if (sigIt != signatures.end()) {
                    auto ordinal = static_cast<size_t>(std::distance(signatures.begin(), sigIt));
                    methods[ordinal] = methodIdx;
                } else {
                    signatures.push_back(signature);
                    methods.push_back(methodIdx);
                }
            }
        }

        this->classMethodIndices[node.name] = meta.methods;
        this->classStaticMethodIndices[node.name] = meta.staticMethods;
        this->chunk.classes.push_back(std::move(meta));
        this->bodyOrder.emplace_back(std::ref(node));
    }

    void Compiler::compileClassBodies(ClassNode& node) {
        auto classIt = this->classIndices.find(node.name);
        if (classIt == this->classIndices.end() || !this->registeredClasses.contains(classIt->second))
            return;

        int classIdx = classIt->second;

        for (const auto& method : node.methods) {
            ir::MethodMeta mm;
            mm.name = method.name;
            mm.classIndex = classIdx;
            mm.argCount = static_cast<int>(method.params.size());

            int bodyIdx = static_cast<int>(this->chunk.methodBodies.size());
            auto body = std::make_unique<MirChunk>();
            MirChunk& bodyChunk = *body;
            this->chunk.methodBodies.push_back(std::move(body));

            std::reference_wrapper<MirChunk> saved = this->current;
            this->current = std::ref(bodyChunk);
            auto loops = this->suspendLoops();

            this->pushScope(true, 0);
            for (const auto& param : method.params)
                this->declareSlot(param.name);

            if (method.isConstructor && !node.baseClassName.empty() && !method.hasSuperCall) {
                int ctorIdx = -1;
                auto baseIt = this->classIndices.find(node.baseClassName);
                int walkIdx = baseIt == this->classIndices.end() ? -1 : baseIt->second;
                while (walkIdx >= 0) {
                    const auto& walkCls = this->chunk.classes[walkIdx];
                    if (walkCls.constructorIndex != -1) {
                        ctorIdx = walkCls.constructorIndex;
                        break;
                    }
                    walkIdx = walkCls.baseClassIndex;
                }

                int argCount = ctorIdx >= 0 ? this->chunk.methods[ctorIdx].argCount : 0;
                int superIdx = this->addSuperCall(ctorIdx, argCount);
                this->current.get().emit(MirOp::LOAD_THIS, 0, node.loc);
                this->current.get().emit(MirOp::CALL_SUPER_CTOR, superIdx, node.loc);
                this->current.get().emit(MirOp::POP, 0, node.loc);
            }

            if (method.body)
                method.body->accept(*this);

            this->current.get().emit(MirOp::POP);

            int emptyIdx = this->addConstant(std::string(""));
            this->current.get().emit(MirOp::PUSH_STR, emptyIdx);
            this->current.get().emit(MirOp::RETURN);

            this->current = saved;
            bodyChunk.slotCount = this->closeScope();

            mm.bodyIndex = bodyIdx;

            this->chunk.methods.push_back(std::move(mm));
        }
    }

    std::string Compiler::methodSignature(const MethodDecl& method) const {
        std::string signature = method.name + "(";
        for (size_t i = 0; i < method.paramTypes.size(); ++i) {
            if (i != 0)
                signature += ",";

            signature += typeInfoToString(method.paramTypes[i]);
        }
        signature += ")";
        return signature;
    }

}
