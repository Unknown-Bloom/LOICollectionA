#include <algorithm>
#include <utility>
#include <vector>

#include "LOICollectionA/frontend/runtime/ScriptVMRegistry.h"

namespace LOICollection::frontend::runtime::detail {
    thread_local std::vector<const ScriptVM*> tlsHeldVMs;
}

namespace LOICollection::frontend::runtime {
    ScriptVMSession::ScriptVMSession(
        ScriptVM& vm,
        std::shared_ptr<GlobalsTable> globals,
        const sandbox::SandboxBudget& budget
    ) : mLock(vm.lock()),
        mVM(vm) {
        detail::tlsHeldVMs.push_back(&vm);

        mVM.diagnostics().clear();
        mVM.vm().reset();
        mVM.vm().setHeap(ScriptHeapRegistry::getInstance().forScript(mVM.scriptId()));
        mVM.vm().setGlobals(std::move(globals));
        mVM.vm().setBudget(budget);
    }

    ScriptVMSession::~ScriptVMSession() {
        mVM.vm().reset();
        std::erase(detail::tlsHeldVMs, &this->mVM);
    }

    bool ScriptVMSession::holds(const ScriptVM* vm) {
        const auto& held = detail::tlsHeldVMs;

        return std::find(held.begin(), held.end(), vm) != held.end();
    }

    ScriptVMRegistry& ScriptVMRegistry::getInstance() {
        static ScriptVMRegistry instance;
        return instance;
    }

    std::shared_ptr<ScriptVM> ScriptVMRegistry::tryGetOrCreate(
        const std::string& scriptId, const std::string& ownerId
    ) {
        std::lock_guard<std::mutex> guard(this->mMutex);

        auto& byOwner = this->mVMs[scriptId];
        if (auto it = byOwner.find(ownerId); it != byOwner.end())
            return it->second;

        auto created = std::make_shared<ScriptVM>(scriptId, ownerId);
        byOwner.emplace(ownerId, created);

        return created;
    }

    void ScriptVMRegistry::release(const std::string& scriptId, const std::string& ownerId) {
        std::lock_guard<std::mutex> guard(this->mMutex);

        if (auto it = this->mVMs.find(scriptId); it != this->mVMs.end()) {
            it->second.erase(ownerId);
            if (it->second.empty())
                this->mVMs.erase(it);
        }
    }

    void ScriptVMRegistry::releaseOwner(const std::string& ownerId) {
        std::lock_guard<std::mutex> guard(this->mMutex);

        for (auto it = this->mVMs.begin(); it != this->mVMs.end();) {
            it->second.erase(ownerId);
            if (it->second.empty())
                it = this->mVMs.erase(it);
            else
                ++it;
        }
    }

    void ScriptVMRegistry::releaseScript(const std::string& scriptId) {
        std::lock_guard<std::mutex> guard(this->mMutex);
        this->mVMs.erase(scriptId);
    }

    void ScriptVMRegistry::releaseAll() {
        std::lock_guard<std::mutex> guard(this->mMutex);
        this->mVMs.clear();
    }
}
