#pragma push_macro("MessageBox")
#undef MessageBox

#include <memory>
#include <string>
#include <fstream>
#include <optional>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <cstdint>

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
#include "LOICollectionA/frontend/ir/MirSerializer.h"
#include "LOICollectionA/frontend/ir/Abi.h"

#include "LOICollectionA/utils/core/Sha256.h"

#include "LOICollectionA/frontend/builtin/ui/form/CustomFormClass.h"
#include "LOICollectionA/frontend/builtin/ui/form/MessageBoxClass.h"
#include "LOICollectionA/frontend/builtin/ui/form/PaginatedFormClass.h"
#include "LOICollectionA/frontend/builtin/ui/form/ScriptFormClass.h"

#include "LOICollectionA/include/form/GUIManager.h"
#include "LOICollectionA/base/ServiceProvider.h"
#include "LOICollectionA/frontend/sandbox/ScriptPermission.h"

#include "LOICollectionA/frontend/ir/ChunkCache.h"
#include "LOICollectionA/frontend/runtime/ScriptHeap.h"
#include "LOICollectionA/frontend/runtime/ScriptVMRegistry.h"

namespace LOICollection::form::detail {
    constexpr std::uintmax_t kMaxScriptFileBytes = 64 << 20;

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

    template <typename Handle>
    bool ownedByOtherScript(const std::shared_ptr<Handle>& existing, const std::shared_ptr<Handle>& incoming) {
        return existing && !existing->scriptId.empty() && !incoming->scriptId.empty()
            && existing->scriptId != incoming->scriptId;
    }
}

namespace LOICollection::form {
    using namespace detail;

    struct GUIManager::Impl {
        std::unordered_map<std::string, std::shared_ptr<frontend::ir::MirChunk>> cache;
        std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<frontend::GlobalsTable>>> globals;

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

    std::shared_ptr<frontend::GlobalsTable> GUIManager::scriptGlobals(const std::string& id, Player& player) {
        auto& byPlayer = this->mImpl->globals[id];

        const std::string uuid = player.getUuid().asString();
        if (auto it = byPlayer.find(uuid); it != byPlayer.end())
            return it->second;

        return byPlayer[uuid] = std::make_shared<frontend::GlobalsTable>();
    }

    void GUIManager::releasePlayerGlobals(Player& player) {
        const std::string uuid = player.getUuid().asString();

        for (auto& [_, byPlayer] : this->mImpl->globals)
            byPlayer.erase(uuid);

        frontend::runtime::ScriptVMRegistry::getInstance().releaseOwner(uuid);
    }

    GUIManager& GUIManager::getInstance() {
        static GUIManager instance;
        return instance;
    }

    ll::Expected<std::string> GUIManager::readFile(const std::string& path) {
        std::error_code ec;
        auto size = std::filesystem::file_size(path, ec);
        if (ec)
            return ll::makeErrorCodeError(ec);
        if (size > kMaxScriptFileBytes)
            return ll::makeStringError("Script file exceeds the 64 MiB limit: " + path);

        std::ifstream file(path, std::ios::binary);
        if (!file)
            return ll::makeErrorCodeError(std::make_error_code(std::errc::no_such_file_or_directory));

        std::string content(size, '\0');
        if (!file.read(content.data(), static_cast<std::streamsize>(size)))
            return ll::makeErrorCodeError(std::make_error_code(std::errc::io_error));

        return content;
    }

    ll::Expected<void> GUIManager::writeFile(const std::string& path, const std::string& content) {
        std::ofstream file(path, std::ios::binary);
        if (!file)
            return ll::makeErrorCodeError(std::make_error_code(std::errc::io_error));

        if (!file.write(content.data(), static_cast<std::streamsize>(content.size())))
            return ll::makeErrorCodeError(std::make_error_code(std::errc::io_error));

        return {};
    }

    ll::Expected<void> GUIManager::load(const std::string& id, const std::string& path) {
        frontend::DiagnosticEngine diagnostics;

        using Serializer = frontend::ir::MirSerializer;

        Serializer::Header header;
        header.scriptId = id;
        header.abiFingerprint = frontend::ir::abiFingerprint();

        std::shared_ptr<frontend::ir::MirChunk> compiled;
        bool hasSource = false;

        if (auto content = this->readFile(path); content.has_value()) {
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

            frontend::SemanticAnalyzer analyzer(diagnostics);
            analyzer.analyze(*result->program);

            if (diagnostics.hasErrors())
                return ll::makeStringError(diagnostics.getErrorMessage());

            if (diagnostics.hasWarnings())
                return ll::makeStringError(diagnostics.getWarningMessage());

            frontend::ir::Compiler mCompiler(diagnostics);
            compiled = std::make_shared<frontend::ir::MirChunk>(mCompiler.compile(*result->program));
            if (diagnostics.hasErrors())
                return ll::makeStringError(diagnostics.getErrorMessage());

            header.sourceHash = result->hashes.back();
            header.importHashes.assign(result->hashes.begin(), result->hashes.end() - 1);
            hasSource = true;
        }

        const std::string packagePath = path + ".lcp";

        if (auto blob = this->readFile(packagePath); blob.has_value()) {
            std::string bodyChecksum;

            if (auto chunk = Serializer::deserialize(blob.value(), header, &bodyChecksum)) {
                if (auto debug = this->readFile(packagePath + ".dbg"); debug.has_value())
                    if (!Serializer::attachDebugInfo(*chunk, debug.value(), bodyChecksum))
                        ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                            ->warn("script '{}' has a stale debug package — loaded without debug info", id);

                if (auto old = this->mImpl->cache.find(id); old != this->mImpl->cache.end())
                    frontend::ir::ChunkCacheRegistry::getInstance().release(old->second.get());
                this->mImpl->cache.insert_or_assign(id, std::make_shared<frontend::ir::MirChunk>(std::move(*chunk)));
                this->mImpl->globals.insert_or_assign(id, std::unordered_map<std::string, std::shared_ptr<frontend::GlobalsTable>>{});
                frontend::runtime::ScriptVMRegistry::getInstance().releaseScript(id);
                frontend::runtime::ScriptHeapRegistry::getInstance().release(id);
                warnIfMissingPermission(id);
                return {};
            }
        }

        if (!hasSource)
            return ll::makeStringError(
                "load: Script '" + id + "' has no readable source and no valid bytecode package");

        frontend::ir::Optimizer optimizer;
        optimizer.optimize(*compiled);

        if (diagnostics.hasErrors())
            return ll::makeStringError(diagnostics.getErrorMessage());

        std::string bodyChecksum;
        auto blob = Serializer::serialize(*compiled, header, &bodyChecksum);
        if (blob) {
            this->writeFile(packagePath, *blob);
            if (auto debug = Serializer::serializeDebugInfo(*compiled, bodyChecksum))
                this->writeFile(packagePath + ".dbg", *debug);
        }

        if (auto old = this->mImpl->cache.find(id); old != this->mImpl->cache.end())
            frontend::ir::ChunkCacheRegistry::getInstance().release(old->second.get());
        this->mImpl->cache.insert_or_assign(id, compiled);
        this->mImpl->globals.insert_or_assign(id, std::unordered_map<std::string, std::shared_ptr<frontend::GlobalsTable>>{});
        frontend::runtime::ScriptVMRegistry::getInstance().releaseScript(id);
        warnIfMissingPermission(id);

        return {};
    }

    ll::Expected<void> GUIManager::execute(const std::string& id) {
        auto it = this->mImpl->cache.find(id);
        if (it == this->mImpl->cache.end())
            return ll::makeStringError("execute: No corresponding bytecode cache was found");

        frontend::Context context;
        context.scriptId = id;
        context.params = frontend::Context::withScriptId(std::move(context.params), id);

        return this->runFormScript(it->second, context, id, std::make_shared<frontend::GlobalsTable>());
    }

    ll::Expected<void> GUIManager::runFormScript(
        const std::shared_ptr<const frontend::ir::MirChunk>& cached,
        const frontend::Context& context,
        const std::string& id,
        std::shared_ptr<frontend::GlobalsTable> globals
    ) {
        const auto budget = frontend::sandbox::budgetForScript(id);
        auto pooled = frontend::runtime::ScriptVMRegistry::getInstance().tryGetOrCreate(id, context.ownerId);

        if (!frontend::runtime::ScriptVMSession::holds(pooled.get())) {
            frontend::runtime::ScriptVMSession session(*pooled, std::move(globals), budget);
            session.vm().run(cached, context);
            if (session.diagnostics().hasErrors())
                return ll::makeStringError(session.diagnostics().getErrorMessage());
            return {};
        }

        frontend::DiagnosticEngine diagnostics;
        frontend::ir::VM vm(diagnostics, std::move(globals));
        vm.setHeap(frontend::runtime::ScriptHeapRegistry::getInstance().forScript(id));
        vm.setBudget(budget);
        vm.run(cached, context);
        if (diagnostics.hasErrors())
            return ll::makeStringError(diagnostics.getErrorMessage());
        return {};
    }

    ll::Expected<void> GUIManager::open(const std::string& id, const std::string& formId, Player& player, const frontend::ArrayRef& ctx) {
        const std::string ownerId = player.getUuid().asString();

        if (this->mImpl->cache.contains(id)) {
            auto cached = this->mImpl->cache.at(id);

            auto mCtx = ctx ? ctx : std::make_shared<frontend::ArrayValue>();
            frontend::Context context{ std::ref(player), mCtx };
            context.scriptId = id;
            context.ownerId = ownerId;
            context.params = frontend::Context::withOwnerId(std::move(context.params), ownerId);

            if (auto err = this->runFormScript(cached, context, id, this->scriptGlobals(id, player)); !err.has_value())
                return err;

            std::string uuid = player.getUuid().asString();
            if (this->mImpl->forms.contains(uuid) && this->mImpl->forms.at(uuid).contains(formId))
                return this->switchToCustomForm(formId, player, id);

            if (this->mImpl->boxs.contains(uuid) && this->mImpl->boxs.at(uuid).contains(formId))
                return this->switchToMessageBox(formId, player, id);

            if (this->mImpl->paginatedForms.contains(uuid) && this->mImpl->paginatedForms.at(uuid).contains(formId))
                return this->switchToPaginatedForm(formId, player, id);

            if (this->mImpl->scriptForms.contains(uuid) && this->mImpl->scriptForms.at(uuid).contains(formId))
                return this->switchToScriptForm(formId, player, id);

            return ll::makeStringError("open: Fuzzy matching can't find the specific form type");
        }

        return ll::makeStringError("open: No corresponding bytecode cache was found");
    }

    ll::Expected<void> GUIManager::open(
        const std::string& id, const std::string& formId, GUIManagerType type, Player& player, const frontend::ArrayRef& ctx
    ) {
        const std::string ownerId = player.getUuid().asString();

        if (this->mImpl->cache.contains(id)) {
            auto cached = this->mImpl->cache.at(id);

            auto mCtx = ctx ? ctx : std::make_shared<frontend::ArrayValue>();
            frontend::Context context{ std::ref(player), mCtx };
            context.scriptId = id;
            context.ownerId = ownerId;
            context.params = frontend::Context::withOwnerId(std::move(context.params), ownerId);

            if (auto err = this->runFormScript(cached, context, id, this->scriptGlobals(id, player)); !err.has_value())
                return err;

            switch (type) {
                case GUIManagerType::CustomForm: return this->switchToCustomForm(formId, player, id);
                case GUIManagerType::MessageBox: return this->switchToMessageBox(formId, player, id);
                case GUIManagerType::PaginatedForm: return this->switchToPaginatedForm(formId, player, id);
                case GUIManagerType::ScriptForm: return this->switchToScriptForm(formId, player, id);
            }
        }

        return ll::makeStringError("open: No corresponding bytecode cache was found");
    }

    ll::Expected<void> GUIManager::switchToCustomForm(const std::string& id, Player& player, const std::string& scriptId) {
        auto handle = this->getCustomFormUI(id, player);
        if (!handle.has_value())
            return ll::Unexpected(handle.error());

        if (!scriptId.empty() && !handle.value()->scriptId.empty() && handle.value()->scriptId != scriptId)
            return ll::makeStringError("CustomForm '" + id + "' belongs to another script");

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

        const auto budget = frontend::sandbox::budgetForScript(handle.value()->scriptId);

        auto result = handle.value()->base->show([this, id, handle = handle.value(), player = std::ref(player), budget](ll::ui::ScreenSession::Result closeResult) mutable -> void {
            frontend::DiagnosticEngine diagnostics;
            frontend::CallbackTypeValues values;

            if (closeResult.has_value())
                values.emplace_back(static_cast<int>(*closeResult));
            else
                values.emplace_back(std::monostate{});

            [[maybe_unused]] auto cbResult = frontend::ir::VM::callFunctionRef(
                handle->show, values,
                frontend::Context::withOwnerId(
                    frontend::Context::withScriptId(frontend::Context{ player }.params, handle->scriptId),
                    player.get().getUuid().asString()
                ),
                diagnostics, &budget
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

    ll::Expected<void> GUIManager::switchToMessageBox(const std::string& id, Player& player, const std::string& scriptId) {
        auto handle = this->getMessageBoxUI(id, player);
        if (!handle.has_value())
            return ll::Unexpected(handle.error());

        if (!scriptId.empty() && !handle.value()->scriptId.empty() && handle.value()->scriptId != scriptId)
            return ll::makeStringError("MessageBox '" + id + "' belongs to another script");

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

        const auto budget = frontend::sandbox::budgetForScript(handle.value()->scriptId);

        auto result = handle.value()->base->show([this, id, handle = handle.value(), player = std::ref(player), budget](ll::ui::MessageBox::Result closeResult) mutable -> void {
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
                frontend::Context::withOwnerId(
                    frontend::Context::withScriptId(frontend::Context{ player }.params, handle->scriptId),
                    player.get().getUuid().asString()
                ),
                diagnostics, &budget
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

    ll::Expected<void> GUIManager::switchToPaginatedForm(const std::string& id, Player& player, const std::string& scriptId) {
        auto handle = this->getPaginatedFormUI(id, player);
        if (!handle.has_value())
            return ll::Unexpected(handle.error());

        if (!scriptId.empty() && !handle.value()->scriptId.empty() && handle.value()->scriptId != scriptId)
            return ll::makeStringError("PaginatedForm '" + id + "' belongs to another script");

        if (!handle.value()->base)
            return ll::makeStringError("PaginatedForm is not built");

        const auto budget = frontend::sandbox::budgetForScript(handle.value()->scriptId);

        auto result = handle.value()->base->show([this, id, handle = handle.value(), player = std::ref(player), budget](ll::ui::ScreenSession::Result closeResult) mutable -> void {
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
                    frontend::Context::withOwnerId(
                        frontend::Context::withScriptId(frontend::Context{ player }.params, handle->scriptId),
                        player.get().getUuid().asString()
                    ),
                    diagnostics, &budget
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

    ll::Expected<void> GUIManager::switchToScriptForm(const std::string& id, Player& player, const std::string& scriptId) {
        auto handle = this->getScriptFormUI(id, player);
        if (!handle.has_value())
            return ll::Unexpected(handle.error());

        if (!scriptId.empty() && !handle.value()->scriptId.empty() && handle.value()->scriptId != scriptId)
            return ll::makeStringError("ScriptForm '" + id + "' belongs to another script");

        const auto budget = frontend::sandbox::budgetForScript(handle.value()->scriptId);

        auto finish = [this, id, handle = handle.value(), player = std::ref(player), budget]() mutable -> void {
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
                    frontend::Context::withOwnerId(
                        frontend::Context::withScriptId(frontend::Context{ player }.params, handle->scriptId),
                        player.get().getUuid().asString()
                    ),
                    diagnostics, &budget
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
        auto& byPlayer = this->mImpl->forms[player.getUuid().asString()];
        if (auto it = byPlayer.find(id); it != byPlayer.end() && detail::ownedByOtherScript(it->second, form))
            return;

        byPlayer.insert_or_assign(id, std::move(form));
    }

    void GUIManager::registerMessageBoxUI(const std::string& id, std::shared_ptr<MessageBoxClass::MessageBoxHandle> box, Player& player) {
        auto& byPlayer = this->mImpl->boxs[player.getUuid().asString()];
        if (auto it = byPlayer.find(id); it != byPlayer.end() && detail::ownedByOtherScript(it->second, box))
            return;

        byPlayer.insert_or_assign(id, std::move(box));
    }

    void GUIManager::registerPaginatedFormUI(const std::string& id, std::shared_ptr<PaginatedFormClass::PaginatedFormHandle> form, Player& player) {
        auto& byPlayer = this->mImpl->paginatedForms[player.getUuid().asString()];
        if (auto it = byPlayer.find(id); it != byPlayer.end() && detail::ownedByOtherScript(it->second, form))
            return;

        byPlayer.insert_or_assign(id, std::move(form));
    }

    void GUIManager::registerScriptFormUI(const std::string& id, std::shared_ptr<ScriptFormClass::ScriptFormHandle> form, Player& player) {
        auto& byPlayer = this->mImpl->scriptForms[player.getUuid().asString()];
        if (auto it = byPlayer.find(id); it != byPlayer.end() && detail::ownedByOtherScript(it->second, form))
            return;

        byPlayer.insert_or_assign(id, std::move(form));
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

        this->releasePlayerGlobals(player);
    }

    void GUIManager::releaseAllUI() {
        releaseHandles(this->mImpl->forms);
        releaseHandles(this->mImpl->boxs);
        releaseHandles(this->mImpl->paginatedForms);
        releaseHandles(this->mImpl->scriptForms);

        for (const auto& [_, chunk] : this->mImpl->cache)
            frontend::ir::ChunkCacheRegistry::getInstance().release(chunk.get());

        this->mImpl->cache.clear();
        this->mImpl->globals.clear();

        frontend::runtime::ScriptVMRegistry::getInstance().releaseAll();
        frontend::runtime::ScriptHeapRegistry::getInstance().releaseAll();
        frontend::ir::ChunkCacheRegistry::getInstance().releaseAll();
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

#pragma pop_macro("MessageBox")
