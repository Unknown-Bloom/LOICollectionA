#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/frontend/runtime/ScriptHeap.h"
#include "LOICollectionA/frontend/sandbox/SandboxBudget.h"

namespace LOICollection::frontend::runtime {
    class ScriptVM {
    public:
        ScriptVM(std::string scriptId, std::string ownerId)
            : mScriptId(std::move(scriptId)),
              mOwnerId(std::move(ownerId)),
              mVM(this->mDiagnostics) {}

        [[nodiscard]] const std::string& scriptId() const { return mScriptId; }
        [[nodiscard]] const std::string& ownerId() const { return mOwnerId; }

        [[nodiscard]] ir::VM& vm() { return mVM; }
        [[nodiscard]] DiagnosticEngine& diagnostics() { return mVM.diagnosticsEngine(); }

        [[nodiscard]] std::unique_lock<std::mutex> lock() {
            return std::unique_lock<std::mutex>(this->mMutex);
        }

    private:
        std::string mScriptId;
        std::string mOwnerId;
        DiagnosticEngine mDiagnostics;
        ir::VM mVM;
        std::mutex mMutex;
    };

    class ScriptVMSession {
    public:
        LOICOLLECTION_A_NDAPI ScriptVMSession(
            ScriptVM& vm,
            std::shared_ptr<GlobalsTable> globals,
            const sandbox::SandboxBudget& budget
        );

        LOICOLLECTION_A_API ~ScriptVMSession();

        ScriptVMSession(const ScriptVMSession&) = delete;
        ScriptVMSession& operator=(const ScriptVMSession&) = delete;

        LOICOLLECTION_A_NDAPI static bool holds(const ScriptVM* vm);

        [[nodiscard]] ir::VM& vm() { return mVM.vm(); }
        [[nodiscard]] DiagnosticEngine& diagnostics() { return mVM.diagnostics(); }

    private:
        std::unique_lock<std::mutex> mLock;
        ScriptVM& mVM;
    };

    class ScriptVMRegistry {
    public:
        LOICOLLECTION_A_NDAPI static ScriptVMRegistry& getInstance();

        LOICOLLECTION_A_NDAPI std::shared_ptr<ScriptVM> tryGetOrCreate(
            const std::string& scriptId, const std::string& ownerId
        );

        LOICOLLECTION_A_API void release(const std::string& scriptId, const std::string& ownerId);
        LOICOLLECTION_A_API void releaseOwner(const std::string& ownerId);
        LOICOLLECTION_A_API void releaseScript(const std::string& scriptId);
        LOICOLLECTION_A_API void releaseAll();

    private:
        ScriptVMRegistry() = default;

        std::mutex mMutex;
        std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<ScriptVM>>> mVMs;
    };
}
