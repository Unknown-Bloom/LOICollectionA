#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <unordered_map>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"

namespace LOICollection::frontend {
    namespace {
        bool matchesArgTypes(const CallbackTypeValues& values, const CallbackTypeArgs& types) {
            if (values.size() != types.size())
                return false;

            for (size_t i = 0; i < values.size(); ++i) {
                bool matched = std::visit([&types, i](auto&& arg) -> bool {
                    using T = std::decay_t<decltype(arg)>;

                    if constexpr (std::is_same_v<T, int>)
                        return types[i] == ParamType::INT;
                    else if constexpr (std::is_same_v<T, float>)
                        return types[i] == ParamType::FLOAT;
                    else if constexpr (std::is_same_v<T, std::string>)
                        return types[i] == ParamType::STRING;
                    else if constexpr (std::is_same_v<T, bool>)
                        return types[i] == ParamType::BOOL;
                    else if constexpr (std::is_same_v<T, ObjectRef>)
                        return types[i] == ParamType::OBJECT;
                    else if constexpr (std::is_same_v<T, FunctionRefPtr>)
                        return types[i] == ParamType::FUNCTION;
                    else if constexpr (std::is_same_v<T, ArrayRef>)
                        return types[i] == ParamType::ARRAY;
                    else
                        return false;
                }, values[i]);

                if (!matched)
                    return false;
            }

            return true;
        }

        std::string canonicalSignature(const Signature& sig) {
            std::string out = sig.name + "/" + std::to_string(sig.argsCount) + (sig.isCombination ? "/c" : "/p");
            for (const auto& arg : sig.args)
                out += ":" + std::to_string(static_cast<int>(arg));

            return out;
        }

        std::string canonicalShape(std::vector<std::string> parts) {
            std::sort(parts.begin(), parts.end());

            std::string out;
            for (auto& part : parts)
                out += std::move(part) + "\n";

            return out;
        }
    }

    std::vector<ParamType> valuesToTypes(const CallbackTypeValues& values, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        std::vector<ParamType> argTypes;
        for (const auto& arg : values) {
            std::visit([&argTypes, &diagnostics, &loc](auto&& value) {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, int>)
                    argTypes.push_back(ParamType::INT);
                else if constexpr (std::is_same_v<T, float>)               
                    argTypes.push_back(ParamType::FLOAT);
                else if constexpr (std::is_same_v<T, std::string>)
                    argTypes.push_back(ParamType::STRING);
                else if constexpr (std::is_same_v<T, bool>)
                    argTypes.push_back(ParamType::BOOL);
                else if constexpr (std::is_same_v<T, ObjectRef>)
                    argTypes.push_back(ParamType::OBJECT);
                else if constexpr (std::is_same_v<T, FunctionRefPtr>)
                    argTypes.push_back(ParamType::FUNCTION);
                else if constexpr (std::is_same_v<T, ArrayRef>)
                    argTypes.push_back(ParamType::ARRAY);
                else if constexpr (std::is_same_v<T, std::monostate>)
                    diagnostics.addError(loc, "Empty optional value cannot be passed to a native callback");
                else
                    diagnostics.addError(loc, "Unsupported argument type");
            }, arg);
        }

        return argTypes;
    }

    struct FunctionCall::Impl {
        std::unordered_map<std::string, std::unordered_map<Signature, CallbackFunc, SignatureHasher>> mFunctions;
        std::unordered_map<std::string, std::unordered_map<Signature, CallbackFuncCombination, SignatureHasher>> mFunctionCombinations;

        uint64_t epoch = 1;
    };

    FunctionCall::FunctionCall() : mImpl(std::make_unique<Impl>()) {}
    FunctionCall::~FunctionCall() = default;

    FunctionCall& FunctionCall::getInstance() {
        static FunctionCall instance;
        return instance;
    }

    void FunctionCall::registerFunction(const std::string& namespaces, const std::string& function, CallbackFunc callback, const CallbackTypeArgs& args) {
        Signature sig{ function, args.size(), args, false };

        this->mImpl->mFunctions[namespaces][sig] = std::move(callback);
        ++this->mImpl->epoch;
    }

    void FunctionCall::registerFunction(const std::string& namespaces, const std::string& function, CallbackFuncCombination callback, const CallbackTypeArgs& args) {
        Signature sig{ function, args.size(), args, true };

        this->mImpl->mFunctionCombinations[namespaces][sig] = std::move(callback);
        ++this->mImpl->epoch;
    }

    void FunctionCall::unregisterFunction(const std::string& namespaces, const std::string& function, const CallbackTypeArgs& args, bool isCombination) {
        Signature sig{ function, args.size(), args, isCombination };

        if (isCombination) {
            this->mImpl->mFunctionCombinations[namespaces].erase(sig);
            ++this->mImpl->epoch;

            return;
        }

        this->mImpl->mFunctions[namespaces].erase(sig);
        ++this->mImpl->epoch;
    }

    bool FunctionCall::isRegistered(const std::string& namespaces, const std::string& function, const CallbackTypeArgs& args) const {
        Signature sig{ function, args.size(), args, false };
        bool result = this->mImpl->mFunctions[namespaces].find(sig) != this->mImpl->mFunctions[namespaces].end();

        sig.isCombination = true;
        return result || this->mImpl->mFunctionCombinations[namespaces].find(sig) != this->mImpl->mFunctionCombinations[namespaces].end();
    }

    std::string FunctionCall::exportShape() const {
        std::vector<std::string> parts;

        for (const auto& [namespaces, functions] : this->mImpl->mFunctions)
            for (const auto& entry : functions)
                parts.push_back(namespaces + "::" + canonicalSignature(entry.first));

        for (const auto& [namespaces, functions] : this->mImpl->mFunctionCombinations)
            for (const auto& entry : functions)
                parts.push_back(namespaces + "::" + canonicalSignature(entry.first));

        return canonicalShape(std::move(parts));
    }

   ll::Expected<TypedValue> FunctionCall::callFunction(const std::string& namespaces, const std::string& function, const CallbackTypeValues& args, const CallbackTypePlaces& placeholders, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        std::vector<ParamType> argTypes = valuesToTypes(args, diagnostics, loc);

        if (!this->isRegistered(namespaces, function, argTypes)) {
            diagnostics.addError(loc, "Function not registered: " + namespaces + "::" + function);
            return TypedValue{};
        }

        Signature sig{ function, argTypes.size(), std::move(argTypes), false };
        if (this->mImpl->mFunctions[namespaces].find(sig) != this->mImpl->mFunctions[namespaces].end()) {
            auto result = this->mImpl->mFunctions[namespaces][sig](args);
            if (!result.has_value())
                return ll::makeStringError("Function callback threw: " + result.error().message());

            return result;
        }

        sig.isCombination = true;
        if (this->mImpl->mFunctionCombinations[namespaces].find(sig) != this->mImpl->mFunctionCombinations[namespaces].end()) {
            auto result = this->mImpl->mFunctionCombinations[namespaces][sig](args, placeholders);
            if (!result.has_value())
                return ll::makeStringError("Function callback threw: " + result.error().message());

            return result;
        }

        return TypedValue{};
    }

    ll::Expected<TypedValue> FunctionCall::callFunctionCached(
        const std::string& namespaces, const std::string& function, const CallbackTypeValues& args,
        const CallbackTypePlaces& placeholders, FunctionCallCacheSlot& slot, DiagnosticEngine& diagnostics,
        const SourceLocation& loc
    ) {
        if (slot.valid && slot.epoch == this->mImpl->epoch &&
            slot.namespaces == namespaces && slot.function == function &&
            matchesArgTypes(args, slot.argTypes)) {
            auto result = slot.isCombination
                ? slot.combination(args, placeholders)
                : slot.callback(args);
            if (!result.has_value())
                return ll::makeStringError("Function callback threw: " + result.error().message());

            return result;
        }

        slot.valid = false;

        std::vector<ParamType> argTypes = valuesToTypes(args, diagnostics, loc);
        Signature sig{ function, argTypes.size(), argTypes, false };

        auto funcsIt = this->mImpl->mFunctions.find(namespaces);
        if (funcsIt != this->mImpl->mFunctions.end()) {
            auto cbIt = funcsIt->second.find(sig);
            if (cbIt != funcsIt->second.end()) {
                auto result = cbIt->second(args);
                if (!result.has_value())
                    return ll::makeStringError("Function callback threw: " + result.error().message());

                if (argTypes.size() == args.size()) {
                    slot.namespaces = namespaces;
                    slot.function = function;
                    slot.argTypes = std::move(argTypes);
                    slot.epoch = this->mImpl->epoch;
                    slot.isCombination = false;
                    slot.callback = cbIt->second;
                    slot.valid = true;
                }

                return result;
            }
        }

        sig.isCombination = true;
        auto combosIt = this->mImpl->mFunctionCombinations.find(namespaces);
        if (combosIt != this->mImpl->mFunctionCombinations.end()) {
            auto cbIt = combosIt->second.find(sig);
            if (cbIt != combosIt->second.end()) {
                auto result = cbIt->second(args, placeholders);
                if (!result.has_value())
                    return ll::makeStringError("Function callback threw: " + result.error().message());

                if (argTypes.size() == args.size()) {
                    slot.namespaces = namespaces;
                    slot.function = function;
                    slot.argTypes = std::move(argTypes);
                    slot.epoch = this->mImpl->epoch;
                    slot.isCombination = true;
                    slot.combination = cbIt->second;
                    slot.valid = true;
                }

                return result;
            }
        }

        diagnostics.addError(loc, "Function not registered: " + namespaces + "::" + function);
        return TypedValue{};
    }
    
    struct MacroCall::Impl {
        std::unordered_map<Signature, CallbackFunc, SignatureHasher> mMacros;
        std::unordered_map<Signature, CallbackFuncCombination, SignatureHasher> mMacroCombinations;
    };

    MacroCall::MacroCall() : mImpl(std::make_unique<Impl>()) {}
    MacroCall::~MacroCall() = default;

    MacroCall& MacroCall::getInstance() {
        static MacroCall instance;
        return instance;
    }

    void MacroCall::registerMacro(const std::string& name, CallbackFunc callback, const CallbackTypeArgs& args) {
        Signature sig{ name, args.size(), args, false };

        this->mImpl->mMacros[sig] = callback;
    }

    void MacroCall::registerMacro(const std::string& name, CallbackFuncCombination callback, const CallbackTypeArgs& args) {
        Signature sig{ name, args.size(), args, true };

        this->mImpl->mMacroCombinations[sig] = callback;
    }

    void MacroCall::unregisterMacro(const std::string& name, const CallbackTypeArgs& args, bool isCombination) {
        Signature sig{ name, args.size(), args, isCombination };

        if (isCombination) {
            this->mImpl->mMacroCombinations.erase(sig);

            return;
        }
        
        this->mImpl->mMacros.erase(sig);
    }

    bool MacroCall::isRegistered(const std::string& name, const CallbackTypeArgs& args) const {
        Signature sig{ name, args.size(), args, false };
        bool result = this->mImpl->mMacros.find(sig) != this->mImpl->mMacros.end();

        sig.isCombination = true;
        return result || this->mImpl->mMacroCombinations.find(sig) != this->mImpl->mMacroCombinations.end();
    }

    std::string MacroCall::exportShape() const {
        std::vector<std::string> parts;

        for (const auto& entry : this->mImpl->mMacros)
            parts.push_back("macro " + canonicalSignature(entry.first));

        for (const auto& entry : this->mImpl->mMacroCombinations)
            parts.push_back("macro " + canonicalSignature(entry.first));

        return canonicalShape(std::move(parts));
    }

    ll::Expected<TypedValue> MacroCall::callMacro(const std::string& name, const CallbackTypeValues& args, const CallbackTypePlaces& placeholders, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        std::vector<ParamType> argTypes = valuesToTypes(args, diagnostics, loc);

        if (!this->isRegistered(name, argTypes)) {
            diagnostics.addError(loc, "Macro not registered: " + name);
            return TypedValue{};
        }
        
        Signature sig{ name, argTypes.size(), std::move(argTypes), false };
        if (this->mImpl->mMacros.find(sig) != this->mImpl->mMacros.end()) {
            auto result = this->mImpl->mMacros[sig](args);
            if (!result.has_value())
                return ll::makeStringError("Macro callback threw: " + result.error().message());

            return result;
        }

        sig.isCombination = true;
        if (this->mImpl->mMacroCombinations.find(sig) != this->mImpl->mMacroCombinations.end()) {
            auto result = this->mImpl->mMacroCombinations[sig](args, placeholders);
            if (!result.has_value())
                return ll::makeStringError("Macro callback threw: " + result.error().message());

            return result;
        }

        return TypedValue{};
    }

    struct ClassCall::Impl {
        struct NativeClassInfo {
            std::vector<std::string> fields;
            FieldLayoutPtr layout;
            std::unordered_map<std::string, ValueNode::ValueType> fieldDefaults;
            std::vector<std::string> staticFields;
            std::unordered_map<std::string, ValueNode::ValueType> staticFieldValues;
            std::unordered_map<Signature, NativeConstructor, SignatureHasher> constructors;
            std::unordered_map<Signature, NativeConstructorCombination, SignatureHasher> constructorCombinations;
            std::unordered_map<Signature, NativeMethod, SignatureHasher> methods;
            std::unordered_map<Signature, NativeMethodCombination, SignatureHasher> methodCombinations;
            std::unordered_map<Signature, NativeStaticMethod, SignatureHasher> staticMethods;
            std::unordered_map<Signature, NativeStaticMethodCombination, SignatureHasher> staticMethodCombinations;
            std::unordered_map<Signature, NativeValueMethod, SignatureHasher> valueMethods;
            std::unordered_map<std::string, NativeOperator> operators;
        };

        std::unordered_map<std::string, NativeClassInfo> classes;

        uint64_t epoch = 1;

        static void syncLayout(NativeClassInfo& info) {
            info.layout = std::make_shared<const FieldLayout>(info.fields);
        }

        static ObjectRef instantiate(const std::string& name, const NativeClassInfo& info) {
            auto obj = std::make_shared<Object>();
            obj->className = name;
            obj->classIndex = -1;
            obj->layout = info.layout;
            obj->resize(info.fields.size());

            for (size_t i = 0; i < info.fields.size(); ++i) {
                auto defaultIt = info.fieldDefaults.find(info.fields[i]);
                obj->slots[i] = defaultIt == info.fieldDefaults.end() ? ValueNode::ValueType{} : defaultIt->second;
            }

            return obj;
        }
    };

    ClassCall::ClassCall() : mImpl(std::make_unique<Impl>()) {}
    ClassCall::~ClassCall() = default;

    ClassCall& ClassCall::getInstance() {
        static ClassCall instance;
        return instance;
    }

    FieldLayoutPtr ClassCall::layoutOf(const std::string& name) const {
        auto it = this->mImpl->classes.find(name);
        return it == this->mImpl->classes.end() ? nullptr : it->second.layout;
    }

    void Object::adoptLayout() {
        if (this->layout)
            return;

        FieldLayoutPtr adopted = ClassCall::getInstance().layoutOf(this->className);
        this->layout = adopted
            ? std::move(adopted)
            : std::make_shared<const FieldLayout>(std::vector<std::string>{});

        this->slots.resize(this->layout->names.size());
    }

    void ClassCall::registerClass(const std::string& name, const std::vector<std::string>& fields) {
        auto& info = this->mImpl->classes[name];
        info.fields = fields;
        info.fieldDefaults.clear();

        for (const auto& field : fields)
            info.fieldDefaults[field] = 0;

        Impl::syncLayout(info);
        ++this->mImpl->epoch;
    }

    void ClassCall::registerConstructor(const std::string& name, NativeConstructor callback, const CallbackTypeArgs& args) {
        Signature sig{ name, args.size(), args, false };
        this->mImpl->classes[name].constructors[sig] = std::move(callback);
        ++this->mImpl->epoch;
    }

    void ClassCall::registerConstructor(const std::string& name, NativeConstructorCombination callback, const CallbackTypeArgs& args) {
        Signature sig{ name, args.size(), args, true };
        this->mImpl->classes[name].constructorCombinations[sig] = std::move(callback);
        ++this->mImpl->epoch;
    }

    void ClassCall::registerMethod(const std::string& className, const std::string& method, NativeMethod callback, const CallbackTypeArgs& args) {
        Signature sig{ method, args.size(), args, false };
        this->mImpl->classes[className].methods[sig] = std::move(callback);
        ++this->mImpl->epoch;
    }

    void ClassCall::registerMethod(const std::string& className, const std::string& method, NativeMethodCombination callback, const CallbackTypeArgs& args) {
        Signature sig{ method, args.size(), args, true };
        this->mImpl->classes[className].methodCombinations[sig] = std::move(callback);
        ++this->mImpl->epoch;
    }

    void ClassCall::registerStaticMethod(const std::string& className, const std::string& method, NativeStaticMethod callback, const CallbackTypeArgs& args) {
        Signature sig{ method, args.size(), args, false };
        this->mImpl->classes[className].staticMethods[sig] = std::move(callback);
        ++this->mImpl->epoch;
    }

    void ClassCall::registerStaticMethod(const std::string& className, const std::string& method, NativeStaticMethodCombination callback, const CallbackTypeArgs& args) {
        Signature sig{ method, args.size(), args, true };
        this->mImpl->classes[className].staticMethodCombinations[sig] = std::move(callback);
        ++this->mImpl->epoch;
    }

    void ClassCall::registerField(const std::string& className, const std::string& field) {
        this->registerField(className, field, 0);
    }

    void ClassCall::registerField(const std::string& className, const std::string& field, const TypedValue& defaultValue) {
        auto& info = this->mImpl->classes[className];
        if (std::ranges::find(info.fields, field) == info.fields.end()) {
            info.fields.push_back(field);
            Impl::syncLayout(info);
        }

        info.fieldDefaults[field] = defaultValue;
        ++this->mImpl->epoch;
    }

    void ClassCall::registerStaticField(const std::string& className, const std::string& field) {
        this->registerStaticField(className, field, 0);
    }

    void ClassCall::registerStaticField(const std::string& className, const std::string& field, const TypedValue& defaultValue) {
        auto& info = this->mImpl->classes[className];
        if (std::ranges::find(info.staticFields, field) == info.staticFields.end())
            info.staticFields.push_back(field);

        info.staticFieldValues[field] = defaultValue;
        ++this->mImpl->epoch;
    }

    void ClassCall::registerValueMethod(const std::string& className, const std::string& method, NativeValueMethod callback, const CallbackTypeArgs& args) {
        Signature sig{ method, args.size(), args, false };
        this->mImpl->classes[className].valueMethods[sig] = std::move(callback);
        ++this->mImpl->epoch;
    }

    void ClassCall::registerOperator(const std::string& className, const std::string& op, NativeOperator callback) {
        this->mImpl->classes[className].operators[op] = std::move(callback);
        ++this->mImpl->epoch;
    }

    bool ClassCall::isRegistered(const std::string& name) const {
        return this->mImpl->classes.find(name) != this->mImpl->classes.end();
    }

    bool ClassCall::hasOperator(const std::string& className, const std::string& op) const {
        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return false;

        return it->second.operators.find(op) != it->second.operators.end();
    }

    bool ClassCall::hasField(const std::string& name, const std::string& field) const {
        auto it = this->mImpl->classes.find(name);
        if (it == this->mImpl->classes.end())
            return false;

        const auto& fields = it->second.fields;
        return std::ranges::find(fields, field) != fields.end();
    }

    bool ClassCall::hasStaticField(const std::string& name, const std::string& field) const {
        auto it = this->mImpl->classes.find(name);
        if (it == this->mImpl->classes.end())
            return false;

        const auto& fields = it->second.staticFields;
        return std::ranges::find(fields, field) != fields.end();
    }

    std::vector<std::string> ClassCall::getFields(const std::string& name) const {
        auto it = this->mImpl->classes.find(name);
        return it == this->mImpl->classes.end() ? std::vector<std::string>{} : it->second.fields;
    }

    std::vector<std::string> ClassCall::getStaticFields(const std::string& name) const {
        auto it = this->mImpl->classes.find(name);
        return it == this->mImpl->classes.end() ? std::vector<std::string>{} : it->second.staticFields;
    }

    std::vector<CallbackTypeArgs> ClassCall::getConstructorSignatures(const std::string& name) const {
        std::vector<CallbackTypeArgs> result;
        auto it = this->mImpl->classes.find(name);
        if (it == this->mImpl->classes.end())
            return result;

        for (const auto& [sig, callback] : it->second.constructors)
            result.push_back(sig.args);
        for (const auto& [sig, callback] : it->second.constructorCombinations)
            result.push_back(sig.args);

        return result;
    }

    std::vector<CallbackTypeArgs> ClassCall::getMethodSignatures(const std::string& className, const std::string& method) const {
        std::vector<CallbackTypeArgs> result;
        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return result;

        for (const auto& [sig, callback] : it->second.methods) {
            if (sig.name == method)
                result.push_back(sig.args);
        }
        
        for (const auto& [sig, callback] : it->second.methodCombinations) {
            if (sig.name == method)
                result.push_back(sig.args);
        }

        return result;
    }

    std::vector<CallbackTypeArgs> ClassCall::getStaticMethodSignatures(const std::string& className, const std::string& method) const {
        std::vector<CallbackTypeArgs> result;
        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return result;

        for (const auto& [sig, callback] : it->second.staticMethods) {
            if (sig.name == method)
                result.push_back(sig.args);
        }

        for (const auto& [sig, callback] : it->second.staticMethodCombinations) {
            if (sig.name == method)
                result.push_back(sig.args);
        }

        return result;
    }

    std::vector<CallbackTypeArgs> ClassCall::getValueMethodSignatures(const std::string& className, const std::string& method) const {
        std::vector<CallbackTypeArgs> result;
        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return result;

        for (const auto& [sig, callback] : it->second.valueMethods) {
            if (sig.name == method)
                result.push_back(sig.args);
        }

        return result;
    }

    std::vector<std::string> ClassCall::getClassNames() const {
        std::vector<std::string> names;
        names.reserve(this->mImpl->classes.size());
        for (const auto& [name, info] : this->mImpl->classes)
            names.push_back(name);
        return names;
    }

    std::string ClassCall::exportShape() const {
        std::vector<std::string> parts;

        for (const auto& [name, info] : this->mImpl->classes) {
            for (const auto& field : info.fields)
                parts.push_back("field " + name + "." + field);

            for (const auto& field : info.staticFields)
                parts.push_back("staticField " + name + "." + field);

            for (const auto& entry : info.constructors)
                parts.push_back("constructor " + name + " " + canonicalSignature(entry.first));

            for (const auto& entry : info.constructorCombinations)
                parts.push_back("constructor " + name + " " + canonicalSignature(entry.first));

            for (const auto& entry : info.methods)
                parts.push_back("method " + name + " " + canonicalSignature(entry.first));

            for (const auto& entry : info.methodCombinations)
                parts.push_back("method " + name + " " + canonicalSignature(entry.first));

            for (const auto& entry : info.staticMethods)
                parts.push_back("staticMethod " + name + " " + canonicalSignature(entry.first));

            for (const auto& entry : info.staticMethodCombinations)
                parts.push_back("staticMethod " + name + " " + canonicalSignature(entry.first));

            for (const auto& entry : info.valueMethods)
                parts.push_back("valueMethod " + name + " " + canonicalSignature(entry.first));

            for (const auto& entry : info.operators)
                parts.push_back("operator " + name + " " + entry.first);
        }

        return canonicalShape(std::move(parts));
    }

    ll::Expected<TypedValue> ClassCall::getStaticField(const std::string& className, const std::string& field) const {
        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        auto valueIt = it->second.staticFieldValues.find(field);
        if (valueIt == it->second.staticFieldValues.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        return valueIt->second;
    }

    void ClassCall::setStaticField(const std::string& className, const std::string& field, const TypedValue& value) {
        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return;

        if (std::ranges::find(it->second.staticFields, field) == it->second.staticFields.end())
            return;

        it->second.staticFieldValues[field] = value;
    }

    ll::Expected<ObjectRef> ClassCall::create(
        const std::string& name, const CallbackTypeValues& args, const CallbackTypePlaces& placeholders,
        DiagnosticEngine& diagnostics, const SourceLocation& loc
    ) {
        auto it = this->mImpl->classes.find(name);
        if (it == this->mImpl->classes.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        auto& info = it->second;
        std::vector<ParamType> argTypes = valuesToTypes(args, diagnostics, loc);

        Signature sig{ name, argTypes.size(), argTypes, false };

        if (info.constructors.find(sig) != info.constructors.end()) {
            auto result = info.constructors[sig](args);
            if (!result.has_value())
                return ll::makeStringError("Constructor callback threw: " + result.error().message());

            return result;
        }

        sig.isCombination = true;
        if (info.constructorCombinations.find(sig) != info.constructorCombinations.end()) {
            auto result = info.constructorCombinations[sig](args, placeholders);
            if (!result.has_value())
                return ll::makeStringError("Constructor callback threw: " + result.error().message());

            return result;
        }

        if (info.constructors.empty() && info.constructorCombinations.empty() && args.empty()) {
            auto obj = Impl::instantiate(name, info);

            return obj;
        }

        return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));
    }

    ll::Expected<TypedValue> ClassCall::callMethod(
        const std::string& className, const std::string& method, const CallbackTypeValues& args,
        const ObjectRef& object, const CallbackTypePlaces& placeholders, DiagnosticEngine& diagnostics,
        const SourceLocation& loc
    ) {
        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        auto& info = it->second;
        std::vector<ParamType> argTypes = valuesToTypes(args, diagnostics, loc);

        Signature sig{ method, argTypes.size(), argTypes, false };

        if (info.methods.find(sig) != info.methods.end()) {
            auto result = info.methods[sig](object, args);
            if (!result.has_value()) 
                return ll::makeStringError("Method callback threw: " + result.error().message());
            
            return result;
        }

        sig.isCombination = true;
        if (info.methodCombinations.find(sig) != info.methodCombinations.end()) {
            auto result = info.methodCombinations[sig](object, args, placeholders);
            if (!result.has_value()) 
                return ll::makeStringError("Method callback threw: " + result.error().message());
            
            return result;
        }

        return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));
    }

    ll::Expected<TypedValue> ClassCall::callStaticMethod(
        const std::string& className, const std::string& method, const CallbackTypeValues& args,
        const CallbackTypePlaces& placeholders, DiagnosticEngine& diagnostics, const SourceLocation& loc
    ) {
        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        auto& info = it->second;
        std::vector<ParamType> argTypes = valuesToTypes(args, diagnostics, loc);

        Signature sig{ method, argTypes.size(), argTypes, false };

        if (info.staticMethods.find(sig) != info.staticMethods.end()) {
            auto result = info.staticMethods[sig](args);
            if (!result.has_value())
                return ll::makeStringError("Static method callback threw: " + result.error().message());

            return result;
        }

        sig.isCombination = true;
        if (info.staticMethodCombinations.find(sig) != info.staticMethodCombinations.end()) {
            auto result = info.staticMethodCombinations[sig](args, placeholders);
            if (!result.has_value())
                return ll::makeStringError("Static method callback threw: " + result.error().message());

            return result;
        }

        return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));
    }

    ll::Expected<TypedValue> ClassCall::callValueMethod(
        const std::string& className, const std::string& method, const TypedValue& self, const CallbackTypeValues& args,
        DiagnosticEngine& diagnostics, const SourceLocation& loc
    ) {
        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        auto& info = it->second;
        std::vector<ParamType> argTypes = valuesToTypes(args, diagnostics, loc);

        Signature sig{ method, argTypes.size(), std::move(argTypes), false };
        auto methodIt = info.valueMethods.find(sig);
        if (methodIt == info.valueMethods.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        auto result = methodIt->second(self, args);
        if (!result.has_value())
            return ll::makeStringError("Value method callback threw: " + result.error().message());

        return result;
    }

    ll::Expected<TypedValue> ClassCall::callOperator(
        const std::string& className, const std::string& op, const TypedValue& left, const TypedValue& right,
        DiagnosticEngine& diagnostics, const SourceLocation& loc
    ) {
        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end()) {
            diagnostics.addError(loc, "Unknown class in operator overload: " + className);
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));
        }

        auto opIt = it->second.operators.find(op);
        if (opIt == it->second.operators.end()) {
            diagnostics.addError(loc, "Class '" + className + "' has no overload for operator '" + op + "'");
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));
        }

        auto result = opIt->second(left, right);
        if (!result.has_value())
            return ll::makeStringError("Operator callback threw: " + result.error().message());

        return result;
    }

    ll::Expected<ObjectRef> ClassCall::createCached(
        const std::string& name, const CallbackTypeValues& args, const CallbackTypePlaces& placeholders,
        NativeConstructorCacheSlot& slot, DiagnosticEngine& diagnostics, const SourceLocation& loc
    ) {
        if (slot.valid && slot.epoch == this->mImpl->epoch && slot.className == name &&
            matchesArgTypes(args, slot.argTypes)) {
            auto result = slot.isCombination
                ? slot.combination(args, placeholders)
                : slot.callback(args);
            if (!result.has_value())
                return ll::makeStringError("Constructor callback threw: " + result.error().message());

            return result;
        }

        slot.valid = false;

        auto it = this->mImpl->classes.find(name);
        if (it == this->mImpl->classes.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        auto& info = it->second;
        std::vector<ParamType> argTypes = valuesToTypes(args, diagnostics, loc);

        Signature sig{ name, argTypes.size(), argTypes, false };

        auto ctorIt = info.constructors.find(sig);
        if (ctorIt != info.constructors.end()) {
            auto result = ctorIt->second(args);
            if (!result.has_value())
                return ll::makeStringError("Constructor callback threw: " + result.error().message());

            if (argTypes.size() == args.size()) {
                slot.className = name;
                slot.argTypes = std::move(argTypes);
                slot.epoch = this->mImpl->epoch;
                slot.isCombination = false;
                slot.callback = ctorIt->second;
                slot.valid = true;
            }

            return result;
        }

        sig.isCombination = true;
        auto combIt = info.constructorCombinations.find(sig);
        if (combIt != info.constructorCombinations.end()) {
            auto result = combIt->second(args, placeholders);
            if (!result.has_value())
                return ll::makeStringError("Constructor callback threw: " + result.error().message());

            if (argTypes.size() == args.size()) {
                slot.className = name;
                slot.argTypes = std::move(argTypes);
                slot.epoch = this->mImpl->epoch;
                slot.isCombination = true;
                slot.combination = combIt->second;
                slot.valid = true;
            }

            return result;
        }

        if (info.constructors.empty() && info.constructorCombinations.empty() && args.empty()) {
            auto obj = Impl::instantiate(name, info);

            return obj;
        }

        return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));
    }

    ll::Expected<TypedValue> ClassCall::callMethodCached(
        const std::string& className, const std::string& method, const CallbackTypeValues& args,
        const ObjectRef& object, const CallbackTypePlaces& placeholders, NativeMethodCacheSlot& slot,
        DiagnosticEngine& diagnostics, const SourceLocation& loc
    ) {
        if (slot.valid && slot.epoch == this->mImpl->epoch &&
            slot.className == className && slot.method == method &&
            matchesArgTypes(args, slot.argTypes)) {
            auto result = slot.isCombination
                ? slot.combination(object, args, placeholders)
                : slot.callback(object, args);
            if (!result.has_value())
                return ll::makeStringError("Method callback threw: " + result.error().message());

            return result;
        }

        slot.valid = false;

        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        auto& info = it->second;
        std::vector<ParamType> argTypes = valuesToTypes(args, diagnostics, loc);

        Signature sig{ method, argTypes.size(), argTypes, false };

        auto methodIt = info.methods.find(sig);
        if (methodIt != info.methods.end()) {
            auto result = methodIt->second(object, args);
            if (!result.has_value())
                return ll::makeStringError("Method callback threw: " + result.error().message());

            if (argTypes.size() == args.size()) {
                slot.className = className;
                slot.method = method;
                slot.argTypes = std::move(argTypes);
                slot.epoch = this->mImpl->epoch;
                slot.isCombination = false;
                slot.callback = methodIt->second;
                slot.valid = true;
            }

            return result;
        }

        sig.isCombination = true;
        auto combIt = info.methodCombinations.find(sig);
        if (combIt != info.methodCombinations.end()) {
            auto result = combIt->second(object, args, placeholders);
            if (!result.has_value())
                return ll::makeStringError("Method callback threw: " + result.error().message());

            if (argTypes.size() == args.size()) {
                slot.className = className;
                slot.method = method;
                slot.argTypes = std::move(argTypes);
                slot.epoch = this->mImpl->epoch;
                slot.isCombination = true;
                slot.combination = combIt->second;
                slot.valid = true;
            }

            return result;
        }

        return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));
    }

    ll::Expected<TypedValue> ClassCall::callStaticMethodCached(
        const std::string& className, const std::string& method, const CallbackTypeValues& args,
        const CallbackTypePlaces& placeholders, NativeStaticMethodCacheSlot& slot,
        DiagnosticEngine& diagnostics, const SourceLocation& loc
    ) {
        if (slot.valid && slot.epoch == this->mImpl->epoch &&
            slot.className == className && slot.method == method &&
            matchesArgTypes(args, slot.argTypes)) {
            auto result = slot.isCombination
                ? slot.combination(args, placeholders)
                : slot.callback(args);
            if (!result.has_value())
                return ll::makeStringError("Static method callback threw: " + result.error().message());

            return result;
        }

        slot.valid = false;

        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        auto& info = it->second;
        std::vector<ParamType> argTypes = valuesToTypes(args, diagnostics, loc);

        Signature sig{ method, argTypes.size(), argTypes, false };

        auto methodIt = info.staticMethods.find(sig);
        if (methodIt != info.staticMethods.end()) {
            auto result = methodIt->second(args);
            if (!result.has_value())
                return ll::makeStringError("Static method callback threw: " + result.error().message());

            if (argTypes.size() == args.size()) {
                slot.className = className;
                slot.method = method;
                slot.argTypes = std::move(argTypes);
                slot.epoch = this->mImpl->epoch;
                slot.isCombination = false;
                slot.callback = methodIt->second;
                slot.valid = true;
            }

            return result;
        }

        sig.isCombination = true;
        auto combIt = info.staticMethodCombinations.find(sig);
        if (combIt != info.staticMethodCombinations.end()) {
            auto result = combIt->second(args, placeholders);
            if (!result.has_value())
                return ll::makeStringError("Static method callback threw: " + result.error().message());

            if (argTypes.size() == args.size()) {
                slot.className = className;
                slot.method = method;
                slot.argTypes = std::move(argTypes);
                slot.epoch = this->mImpl->epoch;
                slot.isCombination = true;
                slot.combination = combIt->second;
                slot.valid = true;
            }

            return result;
        }

        return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));
    }

    ll::Expected<TypedValue> ClassCall::callValueMethodCached(
        const std::string& className, const std::string& method, const TypedValue& self, const CallbackTypeValues& args,
        NativeValueMethodCacheSlot& slot, DiagnosticEngine& diagnostics, const SourceLocation& loc
    ) {
        if (slot.valid && slot.epoch == this->mImpl->epoch &&
            slot.className == className && slot.method == method &&
            matchesArgTypes(args, slot.argTypes)) {
            auto result = slot.callback(self, args);
            if (!result.has_value())
                return ll::makeStringError("Value method callback threw: " + result.error().message());

            return result;
        }

        slot.valid = false;

        auto it = this->mImpl->classes.find(className);
        if (it == this->mImpl->classes.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        auto& info = it->second;
        std::vector<ParamType> argTypes = valuesToTypes(args, diagnostics, loc);

        Signature sig{ method, argTypes.size(), std::move(argTypes), false };
        auto methodIt = info.valueMethods.find(sig);
        if (methodIt == info.valueMethods.end())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::invalid_argument));

        auto result = methodIt->second(self, args);
        if (!result.has_value())
            return ll::makeStringError("Value method callback threw: " + result.error().message());

        if (sig.args.size() == args.size()) {
            slot.className = className;
            slot.method = method;
            slot.argTypes = sig.args;
            slot.epoch = this->mImpl->epoch;
            slot.callback = methodIt->second;
            slot.valid = true;
        }

        return result;
    }
}
