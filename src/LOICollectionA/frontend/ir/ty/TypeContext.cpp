#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "LOICollectionA/frontend/ir/ty/TypeContext.h"

namespace LOICollection::frontend::ir::ty::detail {
    void mix(std::uint64_t& seed, std::uint64_t value) {
        seed ^= value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2);
    }

    std::uint64_t hashData(const TyData& data) {
        std::uint64_t seed = static_cast<std::uint64_t>(data.kind);
        mix(seed, static_cast<std::uint64_t>(data.ownership));
        mix(seed, std::hash<std::string>{}(data.name));

        for (const Ty type : data.args)
            mix(seed, type.index());

        mix(seed, data.element.index());
        mix(seed, data.result.index());

        return seed;
    }

    const TyData& errorData() {
        static const TyData data{};
        return data;
    }

    TyData basicData(TyKind kind) {
        TyData data;
        data.kind = kind;
        return data;
    }

    std::string displayType(const TypeContext& context, Ty type) {
        const TyData& data = context.data(type);
        std::string result;

        switch (data.kind) {
            case TyKind::Error:
            case TyKind::Dynamic:
                result = "unknown";
                break;
            case TyKind::Bool:
                result = "bool";
                break;
            case TyKind::Int:
                result = "int";
                break;
            case TyKind::Float:
                result = "float";
                break;
            case TyKind::String:
                result = "string";
                break;
            case TyKind::Void:
                result = "void";
                break;
            case TyKind::None:
                result = "none";
                break;
            case TyKind::Adt: {
                result = "class " + data.name;
                if (!data.args.empty()) {
                    result += "<";
                    for (std::size_t index = 0; index < data.args.size(); ++index) {
                        if (index != 0)
                            result += ", ";
                        result += displayType(context, data.args[index]);
                    }
                    result += ">";
                }
                break;
            }
            case TyKind::Array:
                result = "array";
                break;
            case TyKind::Function:
                result = "function";
                break;
            case TyKind::Optional:
                result = "optional<" + (data.element.valid() ? displayType(context, data.element) : std::string("unknown")) + ">";
                break;
            case TyKind::Variant: {
                result = "variant<";
                for (std::size_t index = 0; index < data.args.size(); ++index) {
                    if (index != 0)
                        result += ",";
                    result += displayType(context, data.args[index]);
                }
                result += ">";
                break;
            }
            case TyKind::Param:
                result = data.name.empty() ? "generic" : data.name;
                break;
            case TyKind::Trait:
                result = "dyn " + data.name;
                break;
            case TyKind::Enum:
                result = "enum " + data.name;
                break;
        }

        if (data.ownership == Ownership::Borrowed)
            return "ref<" + result + ">";

        return result;
    }
}

namespace LOICollection::frontend::ir::ty {
    using namespace detail;

    TypeContext::TypeContext() {
        this->mTypes.reserve(256);
        this->mBuckets.reserve(512);
        this->mError = Ty(this->internIndex(basicData(TyKind::Error)));
        this->mDynamic = Ty(this->internIndex(basicData(TyKind::Dynamic)));
    }

    std::uint32_t TypeContext::internIndex(const TyData& data) const {
        const std::uint64_t hash = hashData(data);
        auto& bucket = this->mBuckets[hash];

        for (const Ty type : bucket) {
            if (this->mTypes[type.index()] == data)
                return type.index();
        }

        if (this->mTypes.size() >= Ty::invalidIndex)
            return 0;

        const std::uint32_t index = static_cast<std::uint32_t>(this->mTypes.size());
        this->mTypes.push_back(data);
        bucket.push_back(Ty(index));

        return index;
    }

    Ty TypeContext::intern(TyData data) {
        return Ty(this->internIndex(data));
    }

    Ty TypeContext::error() {
        return this->mError;
    }

    Ty TypeContext::dynamic() {
        return this->mDynamic;
    }

    Ty TypeContext::primitive(TyKind kind) {
        switch (kind) {
            case TyKind::Bool:
            case TyKind::Int:
            case TyKind::Float:
            case TyKind::String:
            case TyKind::Void:
            case TyKind::None:
                return this->intern(basicData(kind));
            default:
                return this->mError;
        }
    }

    Ty TypeContext::adt(std::string name, std::vector<Ty> args, Ownership ownership) {
        TyData data;
        data.kind = TyKind::Adt;
        data.ownership = ownership;
        data.name = std::move(name);
        data.args = std::move(args);
        return this->intern(std::move(data));
    }

    Ty TypeContext::array(Ty element, Ownership ownership) {
        TyData data;
        data.kind = TyKind::Array;
        data.ownership = ownership;
        data.element = element;
        return this->intern(std::move(data));
    }

    Ty TypeContext::function(std::vector<Ty> parameters, Ty result) {
        TyData data;
        data.kind = TyKind::Function;
        data.args = std::move(parameters);
        data.result = result;
        return this->intern(std::move(data));
    }

    Ty TypeContext::optional(Ty inner, Ownership ownership) {
        TyData data;
        data.kind = TyKind::Optional;
        data.ownership = ownership;
        data.element = inner;
        return this->intern(std::move(data));
    }

    Ty TypeContext::variant(std::vector<Ty> options, Ownership ownership) {
        TyData data;
        data.kind = TyKind::Variant;
        data.ownership = ownership;
        data.args = std::move(options);
        return this->intern(std::move(data));
    }

    Ty TypeContext::trait(std::string name) {
        TyData data;
        data.kind = TyKind::Trait;
        data.name = std::move(name);
        return this->intern(std::move(data));
    }

    Ty TypeContext::param(std::string name) {
        TyData data;
        data.kind = TyKind::Param;
        data.name = std::move(name);
        return this->intern(std::move(data));
    }

    const TyData& TypeContext::data(Ty type) const {
        if (!type.valid() || type.index() >= this->mTypes.size())
            return errorData();

        return this->mTypes[type.index()];
    }

    TyKind TypeContext::kind(Ty type) const {
        return this->data(type).kind;
    }

    Ownership TypeContext::ownership(Ty type) const {
        return this->data(type).ownership;
    }

    bool TypeContext::same(Ty left, Ty right) const {
        return left.valid() && right.valid() && left.index() == right.index();
    }

    bool TypeContext::sameIgnoringOwnership(Ty left, Ty right) const {
        return this->same(this->withoutOwnership(left), this->withoutOwnership(right));
    }

    Ty TypeContext::withoutOwnership(Ty type) const {
        TyData data = this->data(type);
        data.ownership = Ownership::Value;
        return Ty(this->internIndex(data));
    }

    bool TypeContext::isReference(Ty type) const {
        switch (this->kind(type)) {
            case TyKind::Adt:
            case TyKind::Array:
            case TyKind::Function:
            case TyKind::Enum:
            case TyKind::Trait:
            case TyKind::Param:
                return true;
            default:
                return false;
        }
    }

    bool TypeContext::isCopy(Ty type) const {
        const TyData& data = this->data(type);

        switch (data.kind) {
            case TyKind::Optional:
                return data.element.valid() && this->isCopy(data.element);
            case TyKind::Variant:
                return std::all_of(data.args.begin(), data.args.end(), [this](Ty option) -> bool {
                    return this->isCopy(option);
                });
            case TyKind::Adt:
            case TyKind::Array:
            case TyKind::Function:
            case TyKind::Enum:
            case TyKind::Trait:
            case TyKind::Param:
                return false;
            default:
                return true;
        }
    }

    std::string TypeContext::display(Ty type) const {
        return displayType(*this, type);
    }

    Ty TypeContext::lower(const TypeInfo& info) {
        switch (info.kind) {
            case TypeKind::Unknown:
                return this->dynamic();
            case TypeKind::Int:
                return this->primitive(TyKind::Int);
            case TypeKind::Float:
                return this->primitive(TyKind::Float);
            case TypeKind::String:
                return this->primitive(TyKind::String);
            case TypeKind::Bool:
                return this->primitive(TyKind::Bool);
            case TypeKind::Void:
                return this->primitive(TyKind::Void);
            case TypeKind::None:
                return this->primitive(TyKind::None);
            case TypeKind::Object: {
                std::vector<Ty> args;
                args.reserve(info.typeArgs.size());
                for (const TypeInfo& arg : info.typeArgs)
                    args.push_back(this->lower(arg));
                return this->adt(info.className, std::move(args), info.ownership);
            }
            case TypeKind::Array:
                return this->array(this->dynamic(), info.ownership);
            case TypeKind::Function:
                return this->function({}, this->dynamic());
            case TypeKind::Optional:
                return this->optional(info.optionalInner ? this->lower(*info.optionalInner) : this->dynamic(), info.ownership);
            case TypeKind::Variant: {
                std::vector<Ty> options;
                options.reserve(info.variantOptions.size());
                for (const TypeInfo& option : info.variantOptions)
                    options.push_back(this->lower(option));
                return this->variant(std::move(options), info.ownership);
            }
            case TypeKind::Generic:
                return this->param(info.typeVar);
            case TypeKind::Trait:
                return this->trait(info.className);
            case TypeKind::Enum: {
                TyData result;
                result.kind = TyKind::Enum;
                result.ownership = info.ownership;
                result.name = info.className;
                return this->intern(std::move(result));
            }
            case TypeKind::COUNT:
                return this->error();
        }

        return this->error();
    }

    TypeInfo TypeContext::raise(Ty type) const {
        const TyData& data = this->data(type);
        TypeInfo info;

        info.ownership = data.ownership;
        info.className = data.name;

        switch (data.kind) {
            case TyKind::Error:
            case TyKind::Dynamic:
                info.kind = TypeKind::Unknown;
                break;
            case TyKind::Bool:
                info.kind = TypeKind::Bool;
                break;
            case TyKind::Int:
                info.kind = TypeKind::Int;
                break;
            case TyKind::Float:
                info.kind = TypeKind::Float;
                break;
            case TyKind::String:
                info.kind = TypeKind::String;
                break;
            case TyKind::Void:
                info.kind = TypeKind::Void;
                break;
            case TyKind::None:
                info.kind = TypeKind::None;
                break;
            case TyKind::Adt:
                info.kind = TypeKind::Object;
                for (const Ty arg : data.args)
                    info.typeArgs.push_back(this->raise(arg));
                break;
            case TyKind::Array:
                info.kind = TypeKind::Array;
                break;
            case TyKind::Function:
                info.kind = TypeKind::Function;
                break;
            case TyKind::Optional:
                info.kind = TypeKind::Optional;
                info.optionalInner = std::make_shared<TypeInfo>(this->raise(data.element));
                break;
            case TyKind::Variant:
                info.kind = TypeKind::Variant;
                for (const Ty option : data.args)
                    info.variantOptions.push_back(this->raise(option));
                break;
            case TyKind::Param:
                info.kind = TypeKind::Generic;
                info.typeVar = data.name;
                break;
            case TyKind::Trait:
                info.kind = TypeKind::Trait;
                break;
            case TyKind::Enum:
                info.kind = TypeKind::Enum;
                break;
        }

        return info;
    }

    void TypeContext::defineAdt(AdtDef def) {
        if (this->mFinalized || def.name.empty())
            return;

        auto it = this->mAdtIndex.find(def.name);
        if (it == this->mAdtIndex.end()) {
            const std::uint32_t index = static_cast<std::uint32_t>(this->mAdts.size());
            this->mAdtIndex.emplace(def.name, index);
            this->mAdts.push_back(std::move(def));
            return;
        }

        this->mAdts[it->second] = std::move(def);
    }

    void TypeContext::defineEnum(EnumDef def) {
        if (this->mFinalized || def.name.empty())
            return;

        auto it = this->mEnumIndex.find(def.name);
        if (it == this->mEnumIndex.end()) {
            const std::uint32_t index = static_cast<std::uint32_t>(this->mEnums.size());
            this->mEnumIndex.emplace(def.name, index);
            this->mEnums.push_back(std::move(def));
            return;
        }

        this->mEnums[it->second] = std::move(def);
    }

    void TypeContext::finalize() {
        this->mFinalized = true;
    }

    const AdtDef* TypeContext::adt(std::string_view name) const {
        auto it = this->mAdtIndex.find(std::string(name));
        return it == this->mAdtIndex.end() ? nullptr : &this->mAdts[it->second];
    }

    const EnumDef* TypeContext::enumeration(std::string_view name) const {
        auto it = this->mEnumIndex.find(std::string(name));
        return it == this->mEnumIndex.end() ? nullptr : &this->mEnums[it->second];
    }
}
