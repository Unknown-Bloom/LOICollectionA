#include <any>
#include <memory>
#include <string>
#include <utility>
#include <functional>
#include <unordered_map>

#include <ll/api/Expected.h>

#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/base/Cache.h"

#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"
#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/MirLowering.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"


#include "LOICollectionA/include/CallbackUtils.h"

namespace LOICollection::LOICollectionAPI {
    struct CallbackUtils::Impl {
        LRUKCache<std::string, frontend::ir::BytecodeChunk> mCache;

        std::shared_ptr<ll::io::Logger> logger;

        std::unordered_map<std::string, std::function<ll::Expected<frontend::TypedValue>()>> mVariableCommonMap;
        std::unordered_map<std::string, std::function<ll::Expected<frontend::TypedValue>(Player&)>> mVariableMap;
        std::unordered_map<std::string, std::function<ll::Expected<frontend::TypedValue>(const frontend::CallbackTypeValues&)>> mVariableCommonMapParameter;
        std::unordered_map<std::string, std::function<ll::Expected<frontend::TypedValue>(Player&, const frontend::CallbackTypeValues&)>> mVariableMapParameter;

        Impl() : mCache(100, 200, 5) {}
    };

    CallbackUtils::CallbackUtils() : mImpl(std::make_unique<Impl>()) {
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
    }
    CallbackUtils::~CallbackUtils() = default;

    CallbackUtils& CallbackUtils::getInstance() {
        static CallbackUtils instance;
        return instance;
    }

    void CallbackUtils::registerVariable(const std::string& name, std::function<ll::Expected<frontend::TypedValue>()> callback) {
        this->mImpl->mVariableCommonMap.emplace(name, std::move(callback));

        frontend::MacroCall::getInstance().registerMacro(name, [this, name](const frontend::CallbackTypeValues&) -> ll::Expected<frontend::TypedValue> {
            return this->getValueForVariable(name);
        }, {});
    }

    void CallbackUtils::registerVariable(const std::string& name, std::function<ll::Expected<frontend::TypedValue>(Player&)> callback) {
        this->mImpl->mVariableMap.emplace(name, std::move(callback));

        frontend::MacroCall::getInstance().registerMacro(name, [this, name](const frontend::CallbackTypeValues&, const frontend::CallbackTypePlaces& placeholders) -> ll::Expected<frontend::TypedValue> {
            return this->getValueForVariable(name, std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0)));
        }, {});
    }

    void CallbackUtils::registerVariable(const std::string& name, std::function<ll::Expected<frontend::TypedValue>(const frontend::CallbackTypeValues&)> callback, frontend::CallbackTypeArgs args) {
        this->mImpl->mVariableCommonMapParameter.emplace(name, std::move(callback));

        frontend::MacroCall::getInstance().registerMacro(name, [this, name](const frontend::CallbackTypeValues& args) -> ll::Expected<frontend::TypedValue> {
            return this->getValueForVariable(name, args);
        }, args);
    }

    void CallbackUtils::registerVariable(const std::string& name, std::function<ll::Expected<frontend::TypedValue>(Player&, const frontend::CallbackTypeValues&)> callback, frontend::CallbackTypeArgs args) {
        this->mImpl->mVariableMapParameter.emplace(name, std::move(callback));

        frontend::MacroCall::getInstance().registerMacro(name, [this, name](const frontend::CallbackTypeValues& args, const frontend::CallbackTypePlaces& placeholders) -> ll::Expected<frontend::TypedValue> {
            return this->getValueForVariable(name, std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0)), args);
        }, args);
    }

    ll::Expected<frontend::TypedValue> CallbackUtils::getValueForVariable(const std::string& name) {
        auto it = this->mImpl->mVariableCommonMap.find(name);
        return it != this->mImpl->mVariableCommonMap.end() ? it->second() : "None";
    }

    ll::Expected<frontend::TypedValue> CallbackUtils::getValueForVariable(const std::string& name, Player& player) {
        auto it = this->mImpl->mVariableMap.find(name);
        return it != this->mImpl->mVariableMap.end() ? it->second(player) : this->getValueForVariable(name);
    }

    ll::Expected<frontend::TypedValue> CallbackUtils::getValueForVariable(const std::string& name, const frontend::CallbackTypeValues& parameter) {
        auto it = this->mImpl->mVariableCommonMapParameter.find(name);
        return it != this->mImpl->mVariableCommonMapParameter.end() ? it->second(parameter) : "None";
    }

    ll::Expected<frontend::TypedValue> CallbackUtils::getValueForVariable(const std::string& name, Player& player, const frontend::CallbackTypeValues& parameter) {
        auto it = this->mImpl->mVariableMapParameter.find(name);
        return it != this->mImpl->mVariableMapParameter.end() ? it->second(player, parameter) : this->getValueForVariable(name, parameter);
    }

    std::string CallbackUtils::translate(const std::string& str, Player& player) {
        frontend::DiagnosticEngine diagnostics;

        frontend::ir::VM mVM(diagnostics);

        if (this->mImpl->mCache.contains(str)) {
            auto mCached = this->mImpl->mCache.get(str);

            if (mCached.has_value()) {
                auto result = mVM.run(mCached.value(), { std::ref(player) });
                if (diagnostics.hasErrors()) {
                    this->mImpl->logger->error("CallbackUtils: {}", diagnostics.getErrorMessage());
                    diagnostics.clear();
                }
                
                return frontend::ir::VM::valueToString(result);
            }
        }

        frontend::ir::Compiler mCompiler(diagnostics);

        frontend::Lexer mLexer(str, diagnostics);
        frontend::Parser mParser(mLexer, diagnostics);

        auto mAst = mParser.parse();
        if (diagnostics.hasErrors()) {
            this->mImpl->logger->error("CallbackUtils: {}", diagnostics.getErrorMessage());
            return str;
        }

        frontend::SemanticAnalyzer analyzer(diagnostics);
        if (mAst->getType() == frontend::ASTNode::Type::Program)
            analyzer.analyze(static_cast<frontend::ProgramNode&>(*mAst));

        if (diagnostics.hasErrors()) {
            this->mImpl->logger->error("CallbackUtils: {}", diagnostics.getErrorMessage());
            return str;
        }

        if (diagnostics.hasWarnings()) {
            this->mImpl->logger->warn("CallbackUtils: {}", diagnostics.getWarningMessage());
            return str;
        }

        auto mir = std::make_shared<frontend::ir::MirChunk>(mCompiler.compile(*mAst));
        if (diagnostics.hasErrors()) {
            this->mImpl->logger->error("CallbackUtils: {}", diagnostics.getErrorMessage());
            return str;
        }

        frontend::ir::Optimizer optimizer;
        optimizer.optimize(*mir);

        auto bytecode = std::make_shared<frontend::ir::BytecodeChunk>(
            frontend::ir::MirLowering::lower(*mir));

        auto result = mVM.run(bytecode, { std::ref(player) });
        if (diagnostics.hasErrors()) {
            this->mImpl->logger->error("CallbackUtils: {}", diagnostics.getErrorMessage());
            return str;
        }

        this->mImpl->mCache.put(str, bytecode);
        return frontend::ir::VM::valueToString(result);
    }

    std::string CallbackUtils::translate(const std::string& str) {
        frontend::DiagnosticEngine diagnostics;

        frontend::ir::VM mVM(diagnostics);

        if (this->mImpl->mCache.contains(str)) {
            auto mCached = this->mImpl->mCache.get(str);

            if (mCached.has_value()) {
                auto result = mVM.run(mCached.value(), {});
                if (diagnostics.hasErrors()) {
                    this->mImpl->logger->error("CallbackUtils: {}", diagnostics.getErrorMessage());
                    diagnostics.clear();
                }

                return frontend::ir::VM::valueToString(result);
            }
        }

        frontend::ir::Compiler mCompiler(diagnostics);

        frontend::Lexer mLexer(str, diagnostics);
        frontend::Parser mParser(mLexer, diagnostics);

        auto mAst = mParser.parse();
        if (diagnostics.hasErrors()) {
            this->mImpl->logger->error("CallbackUtils: {}", diagnostics.getErrorMessage());
            return str;
        }

        frontend::SemanticAnalyzer analyzer(diagnostics);
        if (mAst->getType() == frontend::ASTNode::Type::Program)
            analyzer.analyze(static_cast<frontend::ProgramNode&>(*mAst));

        if (diagnostics.hasErrors()) {
            this->mImpl->logger->error("CallbackUtils: {}", diagnostics.getErrorMessage());
            return str;
        }

        if (diagnostics.hasWarnings()) {
            this->mImpl->logger->warn("CallbackUtils: {}", diagnostics.getWarningMessage());
            return str;
        }

        auto mir = std::make_shared<frontend::ir::MirChunk>(mCompiler.compile(*mAst));
        if (diagnostics.hasErrors()) {
            this->mImpl->logger->error("CallbackUtils: {}", diagnostics.getErrorMessage());
            return str;
        }

        frontend::ir::Optimizer optimizer;
        optimizer.optimize(*mir);

        auto bytecode = std::make_shared<frontend::ir::BytecodeChunk>(
            frontend::ir::MirLowering::lower(*mir));

        auto result = mVM.run(bytecode, {});
        if (diagnostics.hasErrors()) {
            this->mImpl->logger->error("CallbackUtils: {}", diagnostics.getErrorMessage());
            return str;
        }

        this->mImpl->mCache.put(str, bytecode);
        return frontend::ir::VM::valueToString(result);
    }
}