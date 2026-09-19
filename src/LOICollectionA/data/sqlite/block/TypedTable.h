#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <ll/api/Expected.h>
#include <magic_enum/magic_enum.hpp>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"

namespace LOICollection::data {

    template <class T>
    struct CellCodec;

    template <>
    struct CellCodec<std::string> {
        static ll::Expected<std::string> encode(std::string const& v) { return v; }
        static ll::Expected<std::string> decode(std::string const& s) { return s; }
    };

    template <std::integral T>
    struct CellCodec<T> {
        static ll::Expected<std::string> encode(T v) {
            char buf[32];
            auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), v);
            if (ec != std::errc())
                return ll::makeStringError("cell encode integer failed");
            return std::string(buf, p);
        }
        static ll::Expected<T> decode(std::string const& s) {
            T v{};
            auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
            if (ec != std::errc() || p != s.data() + s.size())
                return ll::makeStringError("cell decode integer failed");
            return v;
        }
    };

    template <std::floating_point T>
    struct CellCodec<T> {
        static ll::Expected<std::string> encode(T v) {
            char buf[64];
            auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), v);
            if (ec != std::errc())
                return ll::makeStringError("cell encode float failed");
            return std::string(buf, p);
        }
        static ll::Expected<T> decode(std::string const& s) {
            double v{};
            auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
            if (ec != std::errc() || p != s.data() + s.size())
                return ll::makeStringError("cell decode float failed");
            return static_cast<T>(v);
        }
    };

    template <>
    struct CellCodec<bool> {
        static ll::Expected<std::string> encode(bool v) { return v ? std::string("1") : std::string("0"); }
        static ll::Expected<bool> decode(std::string const& s) { return s == "1" || s == "true"; }
    };

    template <class E, std::uint32_t Version = 1>
        requires std::is_enum_v<E>
    class TypedTable {
        static constexpr std::size_t N = magic_enum::enum_count<E>();
        static_assert(N > 0, "enum must have at least one enumerator");

        static constexpr PropKey colKey(E c) {
            return static_cast<PropKey>(magic_enum::enum_index(c).value());
        }

        static ll::Expected<BlockProp> makeCell(PropKey key, std::string const& v) {
            BlockProp p;
            p.key = key;
            p.type = PayloadType::Text;
            p.textValue = v;
            return p;
        }
        static ll::Expected<BlockProp> makeCell(PropKey key, bool v) {
            BlockProp p;
            p.key = key;
            p.type = PayloadType::Int;
            p.intValue = v ? 1 : 0;
            return p;
        }
        template <std::integral T>
        static ll::Expected<BlockProp> makeCell(PropKey key, T v) {
            BlockProp p;
            p.key = key;
            p.type = PayloadType::Int;
            p.intValue = static_cast<std::int64_t>(v);
            return p;
        }
        template <std::floating_point T>
        static ll::Expected<BlockProp> makeCell(PropKey key, T v) {
            BlockProp p;
            p.key = key;
            p.type = PayloadType::Real;
            p.realValue = static_cast<double>(v);
            return p;
        }
        template <class T>
            requires(!std::is_same_v<T, std::string> && !std::is_integral_v<T> && !std::is_floating_point_v<T>)
        static ll::Expected<BlockProp> makeCell(PropKey key, T const& v) {
            auto s = CellCodec<T>::encode(v);
            if (!s.has_value())
                return ll::makeStringError(s.error().message());
            BlockProp p;
            p.key = key;
            p.type = PayloadType::Text;
            p.textValue = std::move(s.value());
            return p;
        }

        template <class T>
        static ll::Expected<T> readCell(BlockProp const& p) {
            if constexpr (std::is_same_v<T, std::string>) {
                return p.textValue;
            } else if constexpr (std::is_same_v<T, bool>) {
                return p.intValue != 0;
            } else if constexpr (std::is_integral_v<T>) {
                return static_cast<T>(p.intValue);
            } else if constexpr (std::is_floating_point_v<T>) {
                return static_cast<T>(p.realValue);
            } else {
                return CellCodec<T>::decode(p.textValue);
            }
        }

        BlockRepository& mRepo;
        std::string mName;
        BlockId mRoot = 0;

        [[nodiscard]] ll::Expected<BlockId> root() {
            if (mRoot != 0)
                return mRoot;
            auto r = mRepo.store().load(0, mName);
            if (r.has_value()) {
                mRoot = r.value().id;
                return mRoot;
            }
            auto c = mRepo.store().createBlock(0, 0, mName);
            if (!c.has_value())
                return ll::makeStringError(c.error().message());
            mRoot = c.value();
            return mRoot;
        }

        [[nodiscard]] ll::Expected<BlockId> row(std::string_view rowKey) {
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            auto rec = mRepo.store().load(rid.value(), rowKey);
            if (rec.has_value() && rec.value().state != BlockLifecycle::Deleted)
                return rec.value().id;
            auto c = mRepo.store().createBlock(rid.value(), 0, rowKey);
            if (!c.has_value())
                return ll::makeStringError(c.error().message());
            return c.value();
        }

        explicit TypedTable(BlockRepository& repo, std::string name, BlockId root)
            : mRepo(repo), mName(std::move(name)), mRoot(root) {}

    public:
        struct Key {
            E value;

            static consteval E parse(std::string_view s) {
                auto opt = magic_enum::enum_cast<E>(s);
                if (!opt)
                    throw "unknown column name";
                return *opt;
            }

            constexpr Key(E c) noexcept : value(c) {}
            consteval Key(std::string_view s) : value(parse(s)) {}
        };

        [[nodiscard]] static ll::Expected<TypedTable> open(BlockRepository& repo, std::string_view name) {
            BlockId rootId = 0;
            auto r = repo.store().load(0, name);
            if (r.has_value()) {
                rootId = r.value().id;
            } else {
                auto c = repo.store().createBlock(0, 0, name);
                if (!c.has_value())
                    return ll::makeStringError(c.error().message());
                rootId = c.value();
            }

            std::string metaKey = std::string("schema:") + std::string(name);
            std::string want = std::to_string(Version);
            want.push_back('\x1e');
            want += std::string(magic_enum::enum_type_name<E>());
            want.push_back('\x1e');
            want += std::to_string(N);

            auto stored = repo.metaGet(metaKey);
            if (!stored.has_value())
                return ll::makeStringError(stored.error().message());
            if (stored.value().has_value() && !stored.value()->empty()) {
                std::string_view s = *stored.value();
                auto a = s.find('\x1e');
                auto b = (a == std::string_view::npos) ? std::string_view::npos : s.find('\x1e', a + 1);
                if (a == std::string_view::npos || b == std::string_view::npos
                    || s.substr(0, a) != std::to_string(Version)
                    || s.substr(a + 1, b - a - 1) != magic_enum::enum_type_name<E>()
                    || s.substr(b + 1) != std::to_string(N))
                    return ll::makeErrorCodeError(
                        BlockError::makeErrorCode(BlockError::BlockErrorCode::SchemaMismatch));
            } else {
                auto w = repo.metaSet(metaKey, want);
                if (!w.has_value())
                    return ll::makeStringError(w.error().message());
            }
            return TypedTable(repo, std::string(name), rootId);
        }

        template <class T>
        [[nodiscard]] ll::Expected<void> set(std::string_view rowKey, Key col, T const& v) {
            auto rid = row(rowKey);
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            auto cell = makeCell(colKey(col.value), v);
            if (!cell.has_value())
                return ll::makeStringError(cell.error().message());
            return mRepo.store().setProp(
                rid.value(), cell.value().key, cell.value().type, cell.value().intValue,
                cell.value().realValue, cell.value().textValue);
        }
        template <class T>
        [[nodiscard]] ll::Expected<void> set(std::string_view rowKey, E col, T const& v) {
            return set(rowKey, Key{col}, v);
        }

        template <class T>
        [[nodiscard]] ll::Expected<T> get(std::string_view rowKey, Key col, T const& def = T{}) {
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            auto rec = mRepo.store().load(rid.value(), rowKey);
            if (!rec.has_value() || rec.value().state == BlockLifecycle::Deleted)
                return def;
            PropKey k = colKey(col.value);
            for (auto const& p : rec.value().props) {
                if (p.key == k) {
                    auto v = readCell<T>(p);
                    if (!v.has_value())
                        return ll::makeStringError(v.error().message());
                    return std::move(v.value());
                }
            }
            return def;
        }
        template <class T>
        [[nodiscard]] ll::Expected<T> get(std::string_view rowKey, E col, T const& def = T{}) {
            return get(rowKey, Key{col}, def);
        }

        [[nodiscard]] ll::Expected<void> del(std::string_view rowKey) {
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            auto rec = mRepo.store().load(rid.value(), rowKey);
            if (!rec.has_value() || rec.value().id == 0 || rec.value().state == BlockLifecycle::Deleted)
                return {};
            return mRepo.store().control(rec.value().id, BlockLifecycle::Deleted);
        }

        [[nodiscard]] ll::Expected<bool> has(std::string_view rowKey) {
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            auto rec = mRepo.store().load(rid.value(), rowKey);
            return rec.has_value() && rec.value().state != BlockLifecycle::Deleted;
        }

        [[nodiscard]] ll::Expected<std::vector<std::string>> list() {
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            auto ch = mRepo.store().children(rid.value(), -1);
            if (!ch.has_value())
                return ll::makeStringError(ch.error().message());
            std::vector<std::string> keys;
            for (auto id : ch.value()) {
                auto rec = mRepo.store().load(id);
                if (!rec.has_value())
                    return ll::makeStringError(rec.error().message());
                if (rec.value().state != BlockLifecycle::Deleted)
                    keys.emplace_back(rec.value().name);
            }
            return keys;
        }

        [[nodiscard]] std::string_view name() const noexcept { return mName; }
        [[nodiscard]] static constexpr std::uint32_t version() noexcept { return Version; }
    };

}
