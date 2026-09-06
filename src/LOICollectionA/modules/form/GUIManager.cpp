#include <memory>
#include <string>
#include <fstream>
#include <optional>
#include <filesystem>
#include <functional>
#include <unordered_map>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/ui/form/CustomForm.h>
#include <ll/api/ui/form/MessageBox.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ScriptLoader.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/Abi.h"

#include "LOICollectionA/utils/core/Sha256.h"

#include "LOICollectionA/frontend/builtin/ui/form/CustomFormClass.h"
#include "LOICollectionA/frontend/builtin/ui/form/MessageBoxClass.h"
#include "LOICollectionA/frontend/builtin/ui/form/PaginatedFormClass.h"
#include "LOICollectionA/frontend/builtin/ui/form/ScriptFormClass.h"

#include "LOICollectionA/include/form/GUIManager.h"
#include "LOICollectionA/base/ServiceProvider.h"
#include "LOICollectionA/frontend/sandbox/ScriptPermission.h"

namespace {
    void warnIfMissingPermission(const std::string& id) {
        const auto permission = ServiceProvider::getInstance().getService<LOICollection::frontend::sandbox::ScriptPermissionService>();
        if (permission && !permission->gate().hasEntry(id))
            ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                ->warn("script '{}' loaded without a permission entry — all capabilities denied", id);
    }

    template <typename Registry>
    bool eraseForm(Registry& registry, const std::string& uuid, const std::string& id) {
        auto owner = registry.find(uuid);
        if (owner == registry.end())
            return false;

        if (auto handle = owner->second.find(id); handle != owner->second.end())
            handle->second->release();

        owner->second.erase(id);
        if (owner->second.empty())
            registry.erase(owner);

        return true;
    }

    template <typename Registry>
    void releaseHandles(Registry& registry) {
        for (auto& [_, handles] : registry)
            for (auto& [_, handle] : handles)
                handle->release();

        registry.clear();
    }
}

namespace LOICollection::form {
    struct GUIManager::Impl {
        std::unordered_map<std::string, std::shared_ptr<frontend::ir::MirChunk>> cache;

        std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<CustomFormClass::CustomFormHandle>>> forms;
        std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<MessageBoxClass::MessageBoxHandle>>> boxs;
        std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<PaginatedFormClass::PaginatedFormHandle>>> paginatedForms;
        std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<ScriptFormClass::ScriptFormHandle>>> scriptForms;

        std::unordered_map<std::string, ValueCallback> values;
        std::unordered_map<std::string, RequestCallback> requests;
        std::unordered_map<std::string, Callback> callbacks;
    };

    GUIManager::GUIManager() : mImpl(std::make_unique<Impl>()) {}
    GUIManager::~GUIManager() = default;

    GUIManager& GUIManager::getInstance() {
        static GUIManager instance;
        return instance;
    }

    ll::Expected<std::string> GUIManager::readFile(const std::string& path) {
        std::error_code ec;
        auto size = std::filesystem::file_size(path, ec);
        if (ec)
            return ll::makeErrorCodeError(ec);

        std::ifstream file(path, std::ios::binary);
        if (!file)
            return ll::makeErrorCodeError(std::make_error_code(std::errc::no_such_file_or_directory));

        std::string content(size, '\0');
        if (!file.read(content.data(), static_cast<std::streamsize>(size)))
            return ll::makeErrorCodeError(std::make_error_code(std::errc::io_error));

        return content;
    }

    ll::Expected<void> GUIManager::load(const std::string& id, const std::string& path) {
        frontend::DiagnosticEngine diagnostics;

        const bool hasSource = this->readFile(path).has_value();
        if (!hasSource)
            return ll::makeStringError("load: Script '" + id + "' has no readable source");

        const std::string rootDir = std::filesystem::path(path).parent_path().string();
        auto result = frontend::ScriptLoader::load(
            path, rootDir,
            [this](const std::string& file) -> std::optional<std::string> {
                auto data = this->readFile(file);
                if (!data.has_value())
                    return std::nullopt;

                return data.value();
            },
            diagnostics
        );
        if (!result)
            return ll::makeStringError(diagnostics.getErrorMessage());

        frontend::ir::Compiler mCompiler(diagnostics);

        frontend::SemanticAnalyzer analyzer(diagnostics);
        analyzer.analyze(*result->program);

        if (diagnostics.hasErrors())
            return ll::makeStringError(diagnostics.getErrorMessage());

        if (diagnostics.hasWarnings())
            return ll::makeStringError(diagnostics.getWarningMessage());

        auto mir = std::make_shared<frontend::ir::MirChunk>(mCompiler.compile(*result->program));
        if (diagnostics.hasErrors())
            return ll::makeStringError(diagnostics.getErrorMessage());

        frontend::ir::Optimizer optimizer;
        optimizer.optimize(*mir);

        this->mImpl->cache.insert_or_assign(id, mir);
        warnIfMissingPermission(id);

        return {};
    }

    ll::Expected<void> GUIManager::execute(const std::string& id) {
        auto it = this->mImpl->cache.find(id);
        if (it == this->mImpl->cache.end())
            return ll::makeStringError("execute: No corresponding bytecode cache was found");

        frontend::DiagnosticEngine diagnostics;
        frontend::ir::VM mVM(diagnostics);

        frontend::Context context;
        context.scriptId = id;

        auto result = mVM.run(it->second, context);
        if (diagnostics.hasErrors())
            return ll::makeStringError(diagnostics.getErrorMessage());

        return {};
    }

    ll::Expected<void> GUIManager::open(const std::string& id, const std::string& formId, Player& player, const frontend::ArrayRef& ctx) {
        frontend::DiagnosticEngine diagnostics;

        frontend::ir::VM mVM(diagnostics);

        if (this->mImpl->cache.contains(id)) {
            auto cached = this->mImpl->cache.at(id);

            auto mCtx = ctx ? ctx : std::make_shared<frontend::ArrayValue>();

            frontend::Context context{ std::ref(player), mCtx };
            context.scriptId = id;
            auto result = mVM.run(cached, context);
            if (diagnostics.hasErrors())
                return ll::makeStringError(diagnostics.getErrorMessage());

            std::string uuid = player.getUuid().asString();
            if (this->mImpl->forms.contains(uuid) && this->mImpl->forms.at(uuid).contains(formId))
                return this->switchToCustomForm(formId, player);

            if (this->mImpl->boxs.contains(uuid) && this->mImpl->boxs.at(uuid).contains(formId))
                return this->switchToMessageBox(formId, player); 

            if (this->mImpl->paginatedForms.contains(uuid) && this->mImpl->paginatedForms.at(uuid).contains(formId))
                return this->switchToPaginatedForm(formId, player);

            if (this->mImpl->scriptForms.contains(uuid) && this->mImpl->scriptForms.at(uuid).contains(formId))
                return this->switchToScriptForm(formId, player);

            return ll::makeStringError("open: Fuzzy matching can't find the specific form type");
        }

        return ll::makeStringError("open: No corresponding bytecode cache was found");
    }

    ll::Expected<void> GUIManager::open(
        const std::string& id, const std::string& formId, GUIManagerType type, Player& player, const frontend::ArrayRef& ctx
    ) {
        frontend::DiagnosticEngine diagnostics;

        frontend::ir::VM mVM(diagnostics);

        if (this->mImpl->cache.contains(id)) {
            auto cached = this->mImpl->cache.at(id);

            auto mCtx = ctx ? ctx : std::make_shared<frontend::ArrayValue>();

            frontend::Context context{ std::ref(player), mCtx };
            context.scriptId = id;
            auto result = mVM.run(cached, context);
            if (diagnostics.hasErrors())
                return ll::makeStringError(diagnostics.getErrorMessage());

            switch (type) {
                case GUIManagerType::CustomForm: return this->switchToCustomForm(formId, player);
                case GUIManagerType::MessageBox: return this->switchToMessageBox(formId, player); 
                case GUIManagerType::PaginatedForm: return this->switchToPaginatedForm(formId, player);
                case GUIManagerType::ScriptForm: return this->switchToScriptForm(formId, player);
            }
        }

        return ll::makeStringError("open: No corresponding bytecode cache was found");
    }

    ll::Expected<void> GUIManager::switchToCustomForm(const std::string& id, Player& player) {
        auto handle = this->getCustomFormUI(id, player);
        if (!handle.has_value())
            return ll::Unexpected(handle.error());

        if (!handle.value()->show) {
            auto result = handle.value()->base->show([this, id, handle = handle.value(), player = std::ref(player)](ll::ui::ScreenSession::Result) mutable -> void {
                if (auto current = this->getCustomFormUI(id, player.get());
                    current.has_value() && current.value() == handle)
                    this->unregisterCustomFormUI(id, player.get());
            });
            if (!result)
                return ll::Unexpected(result.error());

            return {};
        }

        auto result = handle.value()->base->show([this, id, handle = handle.value(), player = std::ref(player)](ll::ui::ScreenSession::Result closeResult) mutable -> void {
            frontend::DiagnosticEngine diagnostics;
            frontend::CallbackTypeValues values;

            if (closeResult.has_value())
                values.emplace_back(static_cast<int>(*closeResult));
            else
                values.emplace_back(std::monostate{});

            [[maybe_unused]] auto cbResult = frontend::ir::VM::callFunctionRef(
                handle->show, values,
                frontend::Context::withScriptId(frontend::Context{ player }.params, handle->scriptId),
                diagnostics
            );

            if (diagnostics.hasErrors()) {
                ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                    ->error("CustomForm::show callback: {}", diagnostics.getErrorMessage());
            }
            
            if (auto current = this->getCustomFormUI(id, player.get());
                current.has_value() && current.value() == handle)
                this->unregisterCustomFormUI(id, player.get());
        });

        if (!result)
            return ll::Unexpected(result.error());

        return {};
    }

    ll::Expected<void> GUIManager::switchToMessageBox(const std::string& id, Player& player) {
        auto handle = this->getMessageBoxUI(id, player);
        if (!handle.has_value())
            return ll::Unexpected(handle.error());

        if (!handle.value()->show) {
            auto result = handle.value()->base->show([this, id, handle = handle.value(), player = std::ref(player)](ll::ui::MessageBox::Result) mutable -> void{
                if (auto current = this->getMessageBoxUI(id, player.get());
                    current.has_value() && current.value() == handle)
                    this->unregisterMessageBoxUI(id, player.get());
            });
            if (!result)
                return ll::Unexpected(result.error());

            return {};
        }

        auto result = handle.value()->base->show([this, id, handle = handle.value(), player = std::ref(player)](ll::ui::MessageBox::Result closeResult) mutable -> void {
            frontend::DiagnosticEngine diagnostics;
            frontend::CallbackTypeValues values;

            if (closeResult.has_value()) {
                auto resultObj = std::make_shared<frontend::Object>();
                resultObj->className = "MessageBoxResult";
                resultObj->classIndex = -1;
                resultObj->assign("closeReason", static_cast<int>(closeResult->closeReason));
                resultObj->assign("selection", closeResult->selection
                    ? frontend::TypedValue(static_cast<int>(*closeResult->selection))
                    : frontend::TypedValue(std::monostate{}));
                
                values.emplace_back(resultObj);
            } else {
                values.emplace_back(std::monostate{});
            }

            [[maybe_unused]] auto cbResult = frontend::ir::VM::callFunctionRef(
                handle->show, values,
                frontend::Context::withScriptId(frontend::Context{ player }.params, handle->scriptId),
                diagnostics
            );

            if (diagnostics.hasErrors()) {
                ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                    ->error("MessageBox::show callback: {}", diagnostics.getErrorMessage());
            }
            
            if (auto current = this->getMessageBoxUI(id, player.get());
                current.has_value() && current.value() == handle)
                this->unregisterMessageBoxUI(id, player.get());
        });

        if (!result)
            return ll::Unexpected(result.error());

        return {};
    }

    ll::Expected<void> GUIManager::switchToPaginatedForm(const std::string& id, Player& player) {
        auto handle = this->getPaginatedFormUI(id, player);
        if (!handle.has_value())
            return ll::Unexpected(handle.error());

        if (!handle.value()->base)
            return ll::makeStringError("PaginatedForm is not built");

        auto result = handle.value()->base->show([this, id, handle = handle.value(), player = std::ref(player)](ll::ui::ScreenSession::Result closeResult) mutable -> void {
            auto resultObj = std::make_shared<frontend::Object>();
            resultObj->className = "PaginatedFormResult";
            resultObj->classIndex = -1;

            if (closeResult.has_value())
                resultObj->assign("closeReason", static_cast<int>(*closeResult));
            else
                resultObj->assign("closeReason", std::monostate{});

            resultObj->assign("selection", handle->selection);
            resultObj->assign("selectionIndex", handle->selectionIndex);
            resultObj->assign("page", handle->selectionPage);

            if (handle->show) {
                frontend::DiagnosticEngine diagnostics;
                frontend::CallbackTypeValues values{ resultObj };

                [[maybe_unused]] auto cbResult = frontend::ir::VM::callFunctionRef(
                    handle->show, values,
                    frontend::Context::withScriptId(frontend::Context{ player }.params, handle->scriptId),
                    diagnostics
                );

                if (diagnostics.hasErrors()) {
                    ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                        ->error("PaginatedForm::show callback: {}", diagnostics.getErrorMessage());
                }
            }

            if (auto current = this->getPaginatedFormUI(id, player.get());
                current.has_value() && current.value() == handle)
                this->unregisterPaginatedFormUI(id, player.get());
        });

        if (!result)
            return ll::Unexpected(result.error());

        return {};
    }

    ll::Expected<void> GUIManager::switchToScriptForm(const std::string& id, Player& player) {
        auto handle = this->getScriptFormUI(id, player);
        if (!handle.has_value())
            return ll::Unexpected(handle.error());

        auto finish = [this, id, handle = handle.value(), player = std::ref(player)]() mutable -> void {
            if (handle->pendingSubflow) {
                if (handle->onClosed)
                    handle->onClosed(player.get());

                if (auto current = this->getScriptFormUI(id, player.get());
                    current.has_value() && current.value() == handle)
                    this->unregisterScriptFormUI(id, player.get());
                return;
            }

            if (handle->onClosed)
                handle->onClosed(player.get());

            if (handle->show) {
                frontend::DiagnosticEngine diagnostics;
                frontend::CallbackTypeValues values;

                auto resultObj = handle->makeResult ? handle->makeResult() : nullptr;
                values.emplace_back(resultObj ? frontend::TypedValue(resultObj) : frontend::TypedValue{});

                [[maybe_unused]] auto cbResult = frontend::ir::VM::callFunctionRef(
                    handle->show, values,
                    frontend::Context::withScriptId(frontend::Context{ player }.params, handle->scriptId),
                    diagnostics
                );

                if (diagnostics.hasErrors()) {
                    ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                        ->error("ScriptForm::show callback: {}", diagnostics.getErrorMessage());
                }
            }

            if (auto current = this->getScriptFormUI(id, player.get());
                current.has_value() && current.value() == handle)
                this->unregisterScriptFormUI(id, player.get());
        };

        if (handle.value()->base) {
            auto result = handle.value()->base->show([finish](ll::ui::ScreenSession::Result) mutable -> void {
                finish();
            });
            if (!result)
                return ll::Unexpected(result.error());

            return {};
        }

        if (handle.value()->box) {
            auto result = handle.value()->box->show([finish, handle = handle.value()](ll::ui::MessageBox::Result closeResult) mutable -> void {
                if (handle->onBoxResult)
                    handle->onBoxResult(closeResult);

                finish();
            });
            if (!result)
                return ll::Unexpected(result.error());

            return {};
        }

        return ll::makeStringError("ScriptForm is not built");
    }

    void GUIManager::registerCustomFormUI(const std::string& id, std::shared_ptr<CustomFormClass::CustomFormHandle> form, Player& player) {
        auto [it, _] = this->mImpl->forms.try_emplace(player.getUuid().asString());
        it->second.insert_or_assign(id, std::move(form));
    }

    void GUIManager::registerMessageBoxUI(const std::string& id, std::shared_ptr<MessageBoxClass::MessageBoxHandle> box, Player& player) {
        auto [it, _] = this->mImpl->boxs.try_emplace(player.getUuid().asString());
        it->second.insert_or_assign(id, std::move(box));
    }

    void GUIManager::registerPaginatedFormUI(const std::string& id, std::shared_ptr<PaginatedFormClass::PaginatedFormHandle> form, Player& player) {
        auto [it, _] = this->mImpl->paginatedForms.try_emplace(player.getUuid().asString());
        it->second.insert_or_assign(id, std::move(form));
    }

    void GUIManager::registerScriptFormUI(const std::string& id, std::shared_ptr<ScriptFormClass::ScriptFormHandle> form, Player& player) {
        auto [it, _] = this->mImpl->scriptForms.try_emplace(player.getUuid().asString());
        it->second.insert_or_assign(id, std::move(form));
    }

    bool GUIManager::unregisterCustomFormUI(const std::string& id, Player& player) {
        return eraseForm(this->mImpl->forms, player.getUuid().asString(), id);
    }

    bool GUIManager::unregisterPaginatedFormUI(const std::string& id, Player& player) {
        return eraseForm(this->mImpl->paginatedForms, player.getUuid().asString(), id);
    }

    bool GUIManager::unregisterMessageBoxUI(const std::string& id, Player& player) {
        return eraseForm(this->mImpl->boxs, player.getUuid().asString(), id);
    }

    bool GUIManager::unregisterScriptFormUI(const std::string& id, Player& player) {
        return eraseForm(this->mImpl->scriptForms, player.getUuid().asString(), id);
    }

    void GUIManager::releasePlayerUI(Player& player) {
        const std::string uuid = player.getUuid().asString();

        auto drop = [&uuid](auto& registry) {
            if (auto it = registry.find(uuid); it != registry.end()) {
                for (auto& [_, handle] : it->second)
                    handle->release();

                registry.erase(it);
            }
        };

        drop(this->mImpl->forms);
        drop(this->mImpl->boxs);
        drop(this->mImpl->paginatedForms);
        drop(this->mImpl->scriptForms);
    }

    void GUIManager::releaseAllUI() {
        this->mImpl->cache.clear();

        releaseHandles(this->mImpl->forms);
        releaseHandles(this->mImpl->boxs);
        releaseHandles(this->mImpl->paginatedForms);
        releaseHandles(this->mImpl->scriptForms);
    }

    ll::Expected<std::shared_ptr<CustomFormClass::CustomFormHandle>> GUIManager::getCustomFormUI(const std::string& id, Player& player) {
        if (auto it = this->mImpl->forms.find(player.getUuid().asString()); it != this->mImpl->forms.end()) {
            auto& innerMap = it->second;
            if (auto innerIt = innerMap.find(id); innerIt != innerMap.end())
                return innerIt->second;
            
            return ll::makeStringError("Form not found for player: " + id);
        }

        return ll::makeStringError("Player has no registered forms");
    }

    ll::Expected<std::shared_ptr<MessageBoxClass::MessageBoxHandle>> GUIManager::getMessageBoxUI(const std::string& id, Player& player) {
        if (auto it = this->mImpl->boxs.find(player.getUuid().asString()); it != this->mImpl->boxs.end()) {
            auto& innerMap = it->second;
            if (auto innerIt = innerMap.find(id); innerIt != innerMap.end())
                return innerIt->second;
            
            return ll::makeStringError("MessageBox not found for player: " + id);
        }

        return ll::makeStringError("Player has no registered messageboxs");
    }

    ll::Expected<std::shared_ptr<PaginatedFormClass::PaginatedFormHandle>> GUIManager::getPaginatedFormUI(const std::string& id, Player& player) {
        if (auto it = this->mImpl->paginatedForms.find(player.getUuid().asString()); it != this->mImpl->paginatedForms.end()) {
            auto& innerMap = it->second;
            if (auto innerIt = innerMap.find(id); innerIt != innerMap.end())
                return innerIt->second;

            return ll::makeStringError("PaginatedForm not found for player: " + id);
        }

        return ll::makeStringError("Player has no registered paginated forms");
    }

    ll::Expected<std::shared_ptr<ScriptFormClass::ScriptFormHandle>> GUIManager::getScriptFormUI(const std::string& id, Player& player) {
        if (auto it = this->mImpl->scriptForms.find(player.getUuid().asString()); it != this->mImpl->scriptForms.end()) {
            auto& innerMap = it->second;
            if (auto innerIt = innerMap.find(id); innerIt != innerMap.end())
                return innerIt->second;

            return ll::makeStringError("ScriptForm not found for player: " + id);
        }

        return ll::makeStringError("Player has no registered script forms");
    }

    void GUIManager::registerValue(const std::string& id, ValueCallback callback) {
        this->mImpl->values.insert_or_assign(id, std::move(callback));
    }

    void GUIManager::registerRequest(const std::string& id, RequestCallback callback) {
        this->mImpl->requests.insert_or_assign(id, std::move(callback));
    }

    void GUIManager::registerCallback(const std::string& id, Callback callback) {
        this->mImpl->callbacks.insert_or_assign(id, std::move(callback));
    }

    ll::Expected<frontend::ArrayRef> GUIManager::getValue(const std::string& id, Player& player) {
        auto it = this->mImpl->values.find(id);
        if (it == this->mImpl->values.end())
            return ll::makeStringError("Value not registered: " + id);

        return it->second(player);
    }

    ll::Expected<frontend::ArrayRef> GUIManager::getRequest(const std::string& id, frontend::ArrayRef args, Player& player) {
        auto it = this->mImpl->requests.find(id);
        if (it == this->mImpl->requests.end())
            return ll::makeStringError("Request not registered: " + id);

        return it->second(args, player);
    }

    ll::Expected<void> GUIManager::getCallback(const std::string& id, frontend::ArrayRef args, Player& player) {
        auto it = this->mImpl->callbacks.find(id);
        if (it == this->mImpl->callbacks.end())
            return ll::makeStringError("Callback not registered: " + id);

        return it->second(args, player);
    }
}
