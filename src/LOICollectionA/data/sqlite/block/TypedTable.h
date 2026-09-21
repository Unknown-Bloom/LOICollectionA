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
#include <unordered_set>
#include <utility>
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

    using RowCells = std::unordered_map<PropKey, BlockProp>;

    namespace {
        std::string_view asByteView(std::vector<std::byte> const& b) noexcept {
            return std::string_view(reinterpret_cast<char const*>(b.data()), b.size());
        }

        std::vector<std::byte> encodeRow(RowCells const& cells) {
            PayloadWriter w;
            for (auto const& [k, c] : cells) {
                PayloadField f;
                f.key = c.key;
                f.type = c.type;
                f.intValue = c.intValue;
                f.realValue = c.realValue;
                f.textValue = c.textValue;
                w.write(f);
            }
            return w.data();
        }

        RowCells decodeRow(std::string_view blob) {
            RowCells cells;
            if (blob.empty())
                return cells;
            PayloadReader reader(blob);
            reader.forEach([&](PayloadField const& f) {
                BlockProp p;
                p.key = f.key;
                p.type = f.type;
                p.intValue = f.intValue;
                p.realValue = f.realValue;
                p.textValue = std::string(f.textValue);
                cells[p.key] = std::move(p);
            });
            return cells;
        }

        RowCells cellsOf(BlockRecord const& rec) {
            if (!rec.payload.empty())
                return decodeRow(asByteView(rec.payload));
            RowCells cells;
            for (auto const& p : rec.props)
                cells[p.key] = p;
            return cells;
        }

        std::string cellToString(BlockProp const& p) {
            switch (p.type) {
                case PayloadType::Int: return std::to_string(p.intValue);
                case PayloadType::Double: {
                    char buf[64];
                    auto [q, ec] = std::to_chars(buf, buf + sizeof(buf), p.realValue);
                    return std::string(buf, q);
                }
                case PayloadType::Text: return p.textValue;
                default: return std::string{};
            }
        }
    }

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
            p.textValue = v ? "1" : "0";
            return p;
        }
        template <std::integral T>
        static ll::Expected<BlockProp> makeCell(PropKey key, T v) {
            BlockProp p;
            p.key = key;
            p.type = PayloadType::Int;
            p.intValue = static_cast<std::int64_t>(v);
            char buf[32];
            auto [q, ec] = std::to_chars(buf, buf + sizeof(buf), p.intValue);
            if (ec != std::errc())
                return ll::makeStringError("cell encode integer failed");
            p.textValue.assign(buf, q);
            return p;
        }
        template <std::floating_point T>
        static ll::Expected<BlockProp> makeCell(PropKey key, T v) {
            BlockProp p;
            p.key = key;
            p.type = PayloadType::Double;
            p.realValue = static_cast<double>(v);
            char buf[64];
            auto [q, ec] = std::to_chars(buf, buf + sizeof(buf), p.realValue);
            if (ec != std::errc())
                return ll::makeStringError("cell encode float failed");
            p.textValue.assign(buf, q);
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

        explicit TypedTable(BlockRepository& repo, std::string name, BlockId root)
            : mRepo(repo), mName(std::move(name)), mRoot(root) {}

    public:
        struct Key {
            E value;

            static consteval E parse(std::string_view s) {
                return *magic_enum::enum_cast<E>(s);
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
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());

            auto cell = makeCell(colKey(col.value), v);
            if (!cell.has_value())
                return ll::makeStringError(cell.error().message());

            RowCells cells;
            auto rec = mRepo.store().load(rid.value(), rowKey);
            if (rec.has_value() && rec.value().state != BlockLifecycle::Deleted)
                cells = cellsOf(rec.value());
            cells[colKey(col.value)] = std::move(cell.value());

            auto blob = encodeRow(cells);
            if (rec.has_value() && rec.value().state != BlockLifecycle::Deleted)
                return mRepo.store().setPayload(rec.value().id, asByteView(blob));
            return mRepo.store().upsertRow(rid.value(), rowKey, asByteView(blob));
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
            auto cells = cellsOf(rec.value());
            auto it = cells.find(colKey(col.value));
            if (it == cells.end())
                return def;
            auto v = readCell<T>(it->second);
            if (!v.has_value())
                return ll::makeStringError(v.error().message());
            return std::move(v.value());
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
            if (conds.empty())
                return list();

            auto ch = mRepo.store().children(rid.value(), -1);
            if (!ch.has_value())
                return ll::makeStringError(ch.error().message());

            std::vector<std::vector<std::string>> groups;
            groups.reserve(conds.size());
            for (auto const& [col, val] : conds) {
                PropKey k = colKey(col.value);
                std::vector<std::string> matched;
                matched.reserve(ch.value().size());
                for (auto id : ch.value()) {
                    auto rec = mRepo.store().load(id);
                    if (!rec.has_value() || rec.value().state == BlockLifecycle::Deleted)
                        continue;
                    auto cells = cellsOf(rec.value());
                    auto it = cells.find(k);
                    if (it != cells.end() && cellToString(it->second) == val)
                        matched.push_back(rec.value().name);
                }
                std::ranges::sort(matched);
                groups.push_back(std::move(matched));
            }

            std::vector<std::string> matched;
            if (mode == FindMode::And) {
                std::ranges::sort(groups, [](auto const& a, auto const& b) { return a.size() < b.size(); });
                std::vector<std::string> acc = groups.front();
                for (size_t i = 1; i < groups.size(); ++i) {
                    std::vector<std::string> next;
                    std::ranges::set_intersection(acc, groups[i], std::back_inserter(next));
                    acc = std::move(next);
                }
                matched = std::move(acc);
            } else {
                std::vector<std::string> acc;
                for (auto& g : groups)
                    acc.insert(acc.end(), g.begin(), g.end());
                std::ranges::sort(acc);
                acc.erase(std::unique(acc.begin(), acc.end()), acc.end());
                matched = std::move(acc);
            }
            return matched;
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
            auto cells = cellsOf(rec.value());
            auto names = magic_enum::enum_names<E>();
            for (auto const& [k, p] : cells) {
                if (static_cast<std::size_t>(k) >= names.size())
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
                out[std::string(names[k])] = std::move(v);
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
            std::unordered_map<std::string, RowCells> mPending;
            std::unordered_set<std::string> mDeleted;

        public:
            Batch(BlockRepository& repo, std::unique_ptr<WriteBatch> tx, BlockId root)
                : mRepo(repo), mTx(std::move(tx)), mRoot(root) {}

            template <class T>
            [[nodiscard]] ll::Expected<void> set(std::string_view rowKey, Key col, T const& v) {
                auto cell = makeCell(colKey(col.value), v);
                if (!cell.has_value())
                    return ll::makeStringError(cell.error().message());
                mPending[std::string(rowKey)][colKey(col.value)] = std::move(cell.value());
                return {};
            }
            template <class T>
            [[nodiscard]] ll::Expected<void> set(std::string_view rowKey, E col, T const& v) {
                return set(rowKey, Key{col}, v);
            }

            template <class T>
            [[nodiscard]] ll::Expected<T> get(std::string_view rowKey, Key col, T const& def = T{}) {
                std::string key(rowKey);
                if (mDeleted.contains(key))
                    return def;
                auto pit = mPending.find(key);
                if (pit != mPending.end()) {
                    auto it = pit->second.find(colKey(col.value));
                    if (it != pit->second.end()) {
                        auto v = readCell<T>(it->second);
                        if (!v.has_value())
                            return ll::makeStringError(v.error().message());
                        return std::move(v.value());
                    }
                }
                auto rec = mRepo.store().load(mRoot, rowKey);
                if (!rec.has_value() || rec.value().state == BlockLifecycle::Deleted)
                    return def;
                auto cells = cellsOf(rec.value());
                auto it = cells.find(colKey(col.value));
                if (it == cells.end())
                    return def;
                auto v = readCell<T>(it->second);
                if (!v.has_value())
                    return ll::makeStringError(v.error().message());
                return std::move(v.value());
            }
            template <class T>
            [[nodiscard]] ll::Expected<T> get(std::string_view rowKey, E col, T const& def = T{}) {
                return get(rowKey, Key{col}, def);
            }

            [[nodiscard]] ll::Expected<bool> has(std::string_view rowKey) {
                std::string key(rowKey);
                if (mDeleted.contains(key))
                    return false;
                if (mPending.contains(key))
                    return true;
                auto r = mRepo.store().load(mRoot, rowKey);
                return r.has_value() && r.value().state != BlockLifecycle::Deleted;
            }

            [[nodiscard]] ll::Expected<void> del(std::string_view rowKey) {
                std::string key(rowKey);
                mDeleted.insert(std::move(key));
                mPending.erase(key);
                return {};
            }

            [[nodiscard]] ll::Expected<bool> commit() {
                for (auto& [rowKey, cells] : mPending) {
                    if (mDeleted.contains(rowKey))
                        continue;
                    if (cells.size() != N) {
                        auto rec = mRepo.store().load(mRoot, rowKey);
                        if (rec.has_value() && rec.value().state != BlockLifecycle::Deleted) {
                            RowCells merged = cellsOf(rec.value());
                            for (auto& [k, p] : cells)
                                merged[k] = p;
                            cells = std::move(merged);
                        }
                    }
                    auto blob = encodeRow(cells);
                    auto r = mTx->upsertRow(mRoot, rowKey, asByteView(blob));
                    if (!r.has_value())
                        return ll::makeStringError(r.error().message());
                }
                for (auto& rowKey : mDeleted) {
                    auto rec = mRepo.store().load(mRoot, rowKey);
                    if (rec.has_value() && rec.value().state != BlockLifecycle::Deleted) {
                        auto r = mTx->control(rec.value().id, BlockLifecycle::Deleted);
                        if (!r.has_value())
                            return ll::makeStringError(r.error().message());
                    }
                }
                return mTx->commit();
            }
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
