#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/AST.h"

namespace LOICollection::frontend::ir::ty {
    enum class TyKind : std::uint8_t {
        Error,
        Dynamic,
        Bool,
        Int,
        Float,
        String,
        Void,
        None,
        Adt,
        Array,
        Function,
        Optional,
        Variant,
        Param,
        Trait,
        Enum
    };

    class Ty {
    public:
        static constexpr std::uint32_t invalidIndex = 0xFFFFFFFFu;

        constexpr Ty() noexcept = default;
        explicit constexpr Ty(std::uint32_t index) noexcept : mIndex(index) {}

        [[nodiscard]] constexpr std::uint32_t index() const noexcept { return mIndex; }
        [[nodiscard]] constexpr bool valid() const noexcept { return mIndex != invalidIndex; }

        friend constexpr bool operator==(Ty left, Ty right) noexcept = default;

    private:
        std::uint32_t mIndex = invalidIndex;
    };

    struct TyData {
        TyKind kind = TyKind::Error;
        Ownership ownership = Ownership::Value;
        std::string name;
        std::vector<Ty> args;
        Ty element{};
        Ty result{};

        bool operator==(const TyData& other) const {
            return kind == other.kind &&
                   ownership == other.ownership &&
                   name == other.name &&
                   args == other.args &&
                   element == other.element &&
                   result == other.result;
        }
    };

    struct FieldDef {
        std::string name;
        Ty type{};
        ValueNode::ValueType defaultValue{};
        bool hasDefault = false;
    };

    struct MethodDef {
        std::string name;
        std::string signature;
        std::vector<Ty> parameters;
        Ty result{};
        int ordinal = -1;
        bool isStatic = false;
    };

    struct AdtDef {
        std::string name;
        Ty base{};
        std::vector<FieldDef> fields;
        std::vector<FieldDef> staticFields;
        std::vector<MethodDef> methods;
        std::vector<MethodDef> staticMethods;
        int constructorOrdinal = -1;
        std::vector<Ty> ancestors;
        std::uint64_t ancestorMask = 0;
    };

    struct VariantDef {
        std::string name;
        std::vector<Ty> payload;
    };

    struct EnumDef {
        std::string name;
        std::vector<VariantDef> variants;
    };

    struct FnSig {
        std::vector<Ty> parameters;
        Ty result{};
    };

    class TypeContext {
    public:
        LOICOLLECTION_A_NDAPI TypeContext();

        LOICOLLECTION_A_NDAPI Ty intern(TyData data);

        LOICOLLECTION_A_NDAPI Ty error();
        LOICOLLECTION_A_NDAPI Ty dynamic();
        LOICOLLECTION_A_NDAPI Ty primitive(TyKind kind);
        LOICOLLECTION_A_NDAPI Ty adt(std::string name, std::vector<Ty> args = {}, Ownership ownership = Ownership::Value);
        LOICOLLECTION_A_NDAPI Ty array(Ty element, Ownership ownership = Ownership::Value);
        LOICOLLECTION_A_NDAPI Ty function(std::vector<Ty> parameters, Ty result);
        LOICOLLECTION_A_NDAPI Ty optional(Ty inner, Ownership ownership = Ownership::Value);
        LOICOLLECTION_A_NDAPI Ty variant(std::vector<Ty> options, Ownership ownership = Ownership::Value);
        LOICOLLECTION_A_NDAPI Ty trait(std::string name);
        LOICOLLECTION_A_NDAPI Ty param(std::string name);

        LOICOLLECTION_A_NDAPI const TyData& data(Ty type) const;
        LOICOLLECTION_A_NDAPI TyKind kind(Ty type) const;
        LOICOLLECTION_A_NDAPI Ownership ownership(Ty type) const;
        LOICOLLECTION_A_NDAPI bool same(Ty left, Ty right) const;
        LOICOLLECTION_A_NDAPI bool sameIgnoringOwnership(Ty left, Ty right) const;
        LOICOLLECTION_A_NDAPI Ty withoutOwnership(Ty type) const;
        LOICOLLECTION_A_NDAPI bool isReference(Ty type) const;
        LOICOLLECTION_A_NDAPI bool isCopy(Ty type) const;
        LOICOLLECTION_A_NDAPI std::string display(Ty type) const;

        LOICOLLECTION_A_NDAPI Ty lower(const TypeInfo& info);
        LOICOLLECTION_A_NDAPI TypeInfo raise(Ty type) const;

        LOICOLLECTION_A_API void defineAdt(AdtDef def);
        LOICOLLECTION_A_API void defineEnum(EnumDef def);
        LOICOLLECTION_A_API void finalize();

        [[nodiscard]] LOICOLLECTION_A_NDAPI const AdtDef* adt(std::string_view name) const;
        [[nodiscard]] LOICOLLECTION_A_NDAPI const EnumDef* enumeration(std::string_view name) const;

    private:
        mutable std::vector<TyData> mTypes;
        mutable std::unordered_map<std::uint64_t, std::vector<Ty>> mBuckets;
        std::vector<AdtDef> mAdts;
        std::vector<EnumDef> mEnums;
        std::unordered_map<std::string, std::uint32_t> mAdtIndex;
        std::unordered_map<std::string, std::uint32_t> mEnumIndex;
        Ty mError;
        Ty mDynamic;
        bool mFinalized = false;

        [[nodiscard]] std::uint32_t internIndex(const TyData& data) const;
    };
}
