#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <ll/api/Expected.h>
#include <magic_enum/magic_enum.hpp>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"

namespace LOICollection::data {

    enum class FindMode { And, Or };

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
            consteval Key(char const* s) : value(parse(std::string_view(s))) {}
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

        [[nodiscard]] static ll::Expected<E> parseColumn(std::string_view s) {
            auto opt = magic_enum::enum_cast<E>(s);
            if (!opt)
                return ll::makeStringError(std::string("unknown column: ").append(s));
            return *opt;
        }

        [[nodiscard]] ll::Expected<std::vector<std::string>> find(
            FindMode mode, std::vector<std::pair<Key, std::string>> const& conds) {
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            if (conds.empty()) {
                if (mode == FindMode::And)
                    return std::vector<std::string>{};
                return list();
            }
            std::vector<std::vector<BlockId>> groups;
            groups.reserve(conds.size());
            for (auto const& [col, val] : conds) {
                auto ids = mRepo.store().queryText(rid.value(), colKey(col.value), val);
                if (!ids.has_value())
                    return ll::makeStringError(ids.error().message());
                groups.push_back(std::move(*ids));
            }
            std::vector<BlockId> matched;
            if (mode == FindMode::And) {
                std::ranges::sort(groups, [](auto const& a, auto const& b) { return a.size() < b.size(); });
                std::vector<BlockId> acc = groups.front();
                for (size_t i = 1; i < groups.size(); ++i) {
                    std::vector<BlockId> next;
                    std::ranges::set_intersection(acc, groups[i], std::back_inserter(next));
                    acc = std::move(next);
                }
                matched = std::move(acc);
            } else {
                std::vector<BlockId> acc;
                for (auto& g : groups)
                    acc.insert(acc.end(), g.begin(), g.end());
                std::ranges::sort(acc);
                acc.erase(std::unique(acc.begin(), acc.end()), acc.end());
                matched = std::move(acc);
            }
            std::vector<std::string> keys;
            keys.reserve(matched.size());
            for (auto id : matched) {
                auto rec = mRepo.store().load(id);
                if (!rec.has_value())
                    return ll::makeStringError(rec.error().message());
                if (rec.value().state != BlockLifecycle::Deleted)
                    keys.push_back(rec.value().name);
            }
            return keys;
        }

        [[nodiscard]] ll::Expected<std::string> findFirst(
            FindMode mode, std::vector<std::pair<Key, std::string>> const& conds) {
            auto keys = find(mode, conds);
            if (!keys.has_value())
                return ll::makeStringError(keys.error().message());
            return keys.value().empty() ? std::string{} : keys.value().front();
        }

        template <class T>
        [[nodiscard]] ll::Expected<std::vector<T>> findValues(
            Key project, FindMode mode, std::vector<std::pair<Key, std::string>> const& conds) {
            auto keys = find(mode, conds);
            if (!keys.has_value())
                return ll::makeStringError(keys.error().message());
            std::vector<T> out;
            out.reserve(keys.value().size());
            for (auto const& k : keys.value()) {
                auto v = get<T>(k, project, T{});
                if (!v.has_value())
                    return ll::makeStringError(v.error().message());
                out.push_back(std::move(v.value()));
            }
            return out;
        }

        [[nodiscard]] ll::Expected<std::unordered_map<std::string, std::string>> getRow(std::string_view rowKey) {
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            auto rec = mRepo.store().load(rid.value(), rowKey);
            if (!rec.has_value())
                return ll::makeStringError(rec.error().message());
            std::unordered_map<std::string, std::string> out;
            if (rec.value().id == 0 || rec.value().state == BlockLifecycle::Deleted)
                return out;
            auto names = magic_enum::enum_names<E>();
            for (auto const& p : rec.value().props) {
                if (static_cast<std::size_t>(p.key) >= names.size())
                    continue;
                std::string v;
                switch (p.type) {
                    case PayloadType::Int: v = std::to_string(p.intValue); break;
                    case PayloadType::Double: {
                        char buf[64];
                        auto [q, ec] = std::to_chars(buf, buf + sizeof(buf), p.realValue);
                        v = std::string(buf, q);
                        break;
                    }
                    case PayloadType::Text: v = p.textValue; break;
                    default: break;
                }
                out[std::string(names[p.key])] = std::move(v);
            }
            return out;
        }

        [[nodiscard]] ll::Expected<void> setRow(
            std::string_view rowKey, std::unordered_map<std::string, std::string> const& cells) {
            for (auto const& [cn, cv] : cells) {
                auto col = parseColumn(cn);
                if (!col.has_value())
                    return ll::makeStringError(col.error().message());
                auto r = set(rowKey, Key{*col}, cv);
                if (!r.has_value())
                    return ll::makeStringError(r.error().message());
            }
            return {};
        }

        class Batch {
            BlockRepository& mRepo;
            std::unique_ptr<WriteBatch> mTx;
            BlockId mRoot;

            [[nodiscard]] ll::Expected<BlockId> rowId(std::string_view rowKey) {
                auto r = mRepo.store().load(mRoot, rowKey);
                if (r.has_value() && r.value().id != 0 && r.value().state != BlockLifecycle::Deleted)
                    return r.value().id;
                auto a = mTx->append(mRoot, 0, rowKey);
                if (!a.has_value())
                    return ll::makeStringError(a.error().message());
                return a.value();
            }

        public:
            Batch(BlockRepository& repo, std::unique_ptr<WriteBatch> tx, BlockId root)
                : mRepo(repo), mTx(std::move(tx)), mRoot(root) {}

            template <class T>
            [[nodiscard]] ll::Expected<void> set(std::string_view rowKey, Key col, T const& v) {
                auto id = rowId(rowKey);
                if (!id.has_value())
                    return ll::makeStringError(id.error().message());
                auto cell = makeCell(colKey(col.value), v);
                if (!cell.has_value())
                    return ll::makeStringError(cell.error().message());
                auto& c = cell.value();
                switch (c.type) {
                    case PayloadType::Int: return mTx->setProp(id.value(), c.key, c.intValue);
                    case PayloadType::Double: return mTx->setProp(id.value(), c.key, c.realValue);
                    default: return mTx->setProp(id.value(), c.key, std::string_view(c.textValue));
                }
            }
            template <class T>
            [[nodiscard]] ll::Expected<void> set(std::string_view rowKey, E col, T const& v) {
                return set(rowKey, Key{col}, v);
            }

            template <class T>
            [[nodiscard]] ll::Expected<T> get(std::string_view rowKey, Key col, T const& def = T{}) {
                auto rec = mRepo.store().load(mRoot, rowKey);
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

            [[nodiscard]] ll::Expected<bool> has(std::string_view rowKey) {
                auto r = mRepo.store().load(mRoot, rowKey);
                return r.has_value() && r.value().state != BlockLifecycle::Deleted;
            }

            [[nodiscard]] ll::Expected<void> del(std::string_view rowKey) {
                auto r = mRepo.store().load(mRoot, rowKey);
                if (!r.has_value() || r.value().state == BlockLifecycle::Deleted)
                    return {};
                return mTx->control(r.value().id, BlockLifecycle::Deleted);
            }

            [[nodiscard]] ll::Expected<bool> commit() { return mTx->commit(); }
            [[nodiscard]] ll::Expected<bool> rollback() { return mTx->rollback(); }
        };

        [[nodiscard]] ll::Expected<Batch> tx() {
            auto b = WriteBatch::begin(mRepo.store());
            if (!b.has_value())
                return ll::makeStringError(b.error().message());
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            return Batch(mRepo, std::move(*b), rid.value());
        }
    };

}
