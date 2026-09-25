#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <mutex>
#include <vector>

#include <SQLiteCpp/SQLiteCpp.h>

#include <ll/api/Expected.h>
#include <magic_enum/magic_enum.hpp>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"

namespace LOICollection::data {

    enum class FindMode { And, Or };

    enum class CellAffinity { Text, Integer, Real };

    template <class T>
    constexpr CellAffinity affinityOf() {
        if constexpr (std::is_floating_point_v<T>)
            return CellAffinity::Real;
        else if constexpr (std::is_integral_v<T>)
            return CellAffinity::Integer;
        else
            return CellAffinity::Text;
    }

    template <class E, E... Cols>
    constexpr std::array<bool, magic_enum::enum_count<E>()> makeIndexed() {
        std::array<bool, magic_enum::enum_count<E>()> a{};
        ((a[*magic_enum::enum_index(Cols)] = true), ...);
        return a;
    }

    template <class E>
    struct TypedIndexPolicy {
        static constexpr auto kIndexed = [] {
            std::array<bool, magic_enum::enum_count<E>()> a{};
            a.fill(true);
            return a;
        }();
    };

    template <class E>
    struct TypedColumn {
        using Types = void;
    };

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

        static constexpr auto kIndexed = TypedIndexPolicy<E>::kIndexed;

        using ColumnTypes = typename TypedColumn<E>::Types;
        static constexpr bool kTypedColumns = !std::is_void_v<ColumnTypes>;

        static constexpr auto kAffinity = [] {
            std::array<CellAffinity, N> a{};
            if constexpr (!std::is_void_v<ColumnTypes>) {
                static_assert(
                    std::tuple_size_v<ColumnTypes> == N, "TypedColumn::Types must list every column");
                [&]<std::size_t... I>(std::index_sequence<I...>) {
                    ((a[I] = affinityOf<std::tuple_element_t<I, ColumnTypes>>()), ...);
                }(std::make_index_sequence<N>{});
            } else {
                a.fill(CellAffinity::Text);
            }
            return a;
        }();

        static constexpr bool indexedAt(PropKey k) {
            return static_cast<std::size_t>(k) < N && kIndexed[static_cast<std::size_t>(k)];
        }
        static constexpr bool indexed(E c) {
            return kIndexed[static_cast<std::size_t>(colKey(c))];
        }
        static std::string indexMask() {
            std::string s;
            for (std::size_t i = 0; i < N; i += 4) {
                int v = 0;
                for (std::size_t j = 0; j < 4; ++j)
                    if (i + j < N && kIndexed[i + j])
                        v |= (1 << j);
                s.push_back("0123456789abcdef"[v]);
            }
            return s;
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
                if (p.type == PayloadType::Text)
                    return p.textValue == "1" || p.textValue == "true";
                return p.intValue != 0;
            } else if constexpr (std::is_integral_v<T>) {
                if (p.type == PayloadType::Text) {
                    T v{};
                    auto [ptr, ec] = std::from_chars(p.textValue.data(), p.textValue.data() + p.textValue.size(), v);
                    if (ec != std::errc() || ptr != p.textValue.data() + p.textValue.size())
                        return ll::makeStringError("cell decode integer failed");
                    return v;
                }
                return static_cast<T>(p.intValue);
            } else if constexpr (std::is_floating_point_v<T>) {
                if (p.type == PayloadType::Text) {
                    double v{};
                    auto [ptr, ec] = std::from_chars(p.textValue.data(), p.textValue.data() + p.textValue.size(), v);
                    if (ec != std::errc())
                        return ll::makeStringError("cell decode float failed");
                    return static_cast<T>(v);
                }
                return static_cast<T>(p.realValue);
            } else {
                return CellCodec<T>::decode(p.textValue);
            }
        }

        BlockRepository& mRepo;
        std::string mName;
        BlockId mRoot = 0;
        std::string mSide;

        mutable std::unordered_set<std::size_t> mIndexedColumns;
        static inline std::mutex sIndexMutex;

        static std::string sideNameOf(BlockId root) { return "col_" + std::to_string(root); }

        static std::string affinityName(CellAffinity a) {
            switch (a) {
                case CellAffinity::Integer: return "INTEGER";
                case CellAffinity::Real: return "REAL";
                default: return "TEXT";
            }
        }

        std::string quoted(std::size_t i) const {
            auto names = magic_enum::enum_names<E>();
            return "\"" + std::string(names[i]) + "\"";
        }

        std::string sideFingerprint() const {
            auto names = magic_enum::enum_names<E>();
            std::string fp;
            for (std::size_t i = 0; i < N; ++i) {
                if (i)
                    fp.push_back(',');
                fp.append(names[i]);
                fp.push_back(':');
                fp.append(affinityName(kAffinity[i]));
            }
            return fp;
        }

        std::string sideDdl() const {
            std::string sql = "CREATE TABLE IF NOT EXISTS \"" + mSide + "\"(id INTEGER PRIMARY KEY";
            for (std::size_t i = 0; i < N; ++i) {
                sql += ",";
                sql += quoted(i);
                sql += " ";
                sql += affinityName(kAffinity[i]);
            }
            sql += ")";
            return sql;
        }

        std::string sideIndexDdl(std::size_t i) const {
            return "CREATE INDEX IF NOT EXISTS \"" + mSide + "_" + std::to_string(i) + "\" ON \"" + mSide
                 + "\"(" + quoted(i) + ")";
        }

        void ensureColumnIndex(std::size_t i) {
            if (!indexedAt(static_cast<PropKey>(i)))
                return;
            std::lock_guard<std::mutex> lk(sIndexMutex);
            if (mIndexedColumns.contains(i))
                return;
            auto r = mRepo.store().exec(sideIndexDdl(i));
            if (r.has_value())
                mIndexedColumns.insert(i);
        }

        std::string cellSql(std::size_t i) const {
            std::string q = quoted(i);
            return "INSERT INTO \"" + mSide + "\"(id," + q + ") VALUES(?,?) ON CONFLICT(id) DO UPDATE SET "
                 + q + "=?";
        }

        std::string cellKey(std::size_t i) const { return "sideSet:" + mSide + ":" + std::to_string(i); }

        static BlockProp serializeCell(BlockProp const& p, CellAffinity a) {
            BlockProp out;
            out.key = p.key;
            if (a == CellAffinity::Integer) {
                out.type = PayloadType::Int;
                if (p.type == PayloadType::Int) {
                    out.intValue = p.intValue;
                } else {
                    std::int64_t v = 0;
                    auto [ptr, ec] = std::from_chars(p.textValue.data(),
                        p.textValue.data() + p.textValue.size(), v);
                    out.intValue = (ec == std::errc()) ? v : 0;
                }
                out.textValue = std::to_string(out.intValue);
            } else if (a == CellAffinity::Real) {
                out.type = PayloadType::Double;
                if (p.type == PayloadType::Double) {
                    out.realValue = p.realValue;
                } else {
                    double v = 0.0;
                    auto [ptr, ec] = std::from_chars(p.textValue.data(),
                        p.textValue.data() + p.textValue.size(), v);
                    out.realValue = (ec == std::errc()) ? v : 0.0;
                }
                char buf[64];
                auto [q, ec] = std::to_chars(buf, buf + sizeof(buf), out.realValue);
                out.textValue.assign(buf, q);
            } else {
                out.type = PayloadType::Text;
                out.textValue = cellToString(p);
            }
            return out;
        }

        static std::optional<BlockProp> readColumn(
            SQLite::Statement& stmt, int col, PropKey key, CellAffinity a) {
            if (stmt.getColumn(col).isNull())
                return std::nullopt;
            BlockProp p;
            p.key = key;
            if (a == CellAffinity::Integer) {
                p.type = PayloadType::Int;
                p.intValue = stmt.getColumn(col).getInt64();
                p.textValue = std::to_string(p.intValue);
            } else if (a == CellAffinity::Real) {
                p.type = PayloadType::Double;
                p.realValue = stmt.getColumn(col).getDouble();
                char buf[64];
                auto [q, ec] = std::to_chars(buf, buf + sizeof(buf), p.realValue);
                p.textValue.assign(buf, q);
            } else {
                p.type = PayloadType::Text;
                p.textValue = stmt.getColumn(col).getString();
            }
            return p;
        }

        ll::Expected<void> writeCell(BlockId id, std::size_t i, BlockProp const& cell) {
            BlockProp idParam;
            idParam.type = PayloadType::Int;
            idParam.intValue = id;
            BlockProp value = serializeCell(cell, kAffinity[i]);
            BlockProp params[3] = {idParam, value, value};
            auto b = WriteBatch::begin(mRepo.store());
            if (!b.has_value())
                return ll::makeStringError(b.error().message());
            auto r = (*b)->execCells(cellKey(i), cellSql(i), params);
            if (!r.has_value())
                return ll::makeStringError(r.error().message());
            auto c = (*b)->commit();
            if (!c.has_value())
                return ll::makeStringError(c.error().message());
            return {};
        }

        [[nodiscard]] ll::Expected<RowCells> readSideCells(BlockId id) {
            std::string sql = "SELECT ";
            for (std::size_t i = 0; i < N; ++i) {
                if (i)
                    sql += ",";
                sql += quoted(i);
            }
            sql += " FROM \"" + mSide + "\" WHERE id=?";

            RowCells cells;
            bool found = false;
            BlockProp idParam;
            idParam.type = PayloadType::Int;
            idParam.intValue = id;
            auto r = mRepo.store().withQuery(
                "sideRow:" + mSide, sql, std::span<const BlockProp>(&idParam, 1),
                [&](SQLite::Statement& stmt) {
                    found = true;
                    for (std::size_t i = 0; i < N; ++i) {
                        auto p = readColumn(stmt, static_cast<int>(i), static_cast<PropKey>(i), kAffinity[i]);
                        if (p.has_value())
                            cells[static_cast<PropKey>(i)] = std::move(*p);
                    }
                });
            if (!r.has_value())
                return ll::makeStringError(r.error().message());
            if (found)
                return cells;

            auto rec = mRepo.store().load(id);
            if (!rec.has_value())
                return ll::makeStringError(rec.error().message());
            return cellsOf(rec.value());
        }

        BlockProp condProp(std::size_t slot, std::string const& val) const {
            BlockProp p;
            p.key = static_cast<PropKey>(slot);
            p.textValue = val;
            if (kAffinity[slot] == CellAffinity::Integer) {
                std::int64_t v{};
                auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), v);
                if (ec == std::errc() && ptr == val.data() + val.size()) {
                    p.type = PayloadType::Int;
                    p.intValue = v;
                    return p;
                }
            } else if (kAffinity[slot] == CellAffinity::Real) {
                double v{};
                auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), v);
                if (ec == std::errc() && ptr == val.data() + val.size()) {
                    p.type = PayloadType::Double;
                    p.realValue = v;
                    return p;
                }
            }
            p.type = PayloadType::Text;
            return p;
        }

        ll::Expected<void> ensureSide() {
            std::string want = sideFingerprint();
            auto stored = mRepo.metaGet("sidecol:" + mName);
            if (!stored.has_value())
                return ll::makeStringError(stored.error().message());
            if (stored.value().has_value() && stored.value().value() == want)
                return {};

            auto b = WriteBatch::begin(mRepo.store());
            if (!b.has_value())
                return ll::makeStringError(b.error().message());
            auto dropped = (*b)->exec("DROP TABLE IF EXISTS \"" + mSide + "\"");
            if (!dropped.has_value())
                return ll::makeStringError(dropped.error().message());
            auto made = (*b)->exec(sideDdl());
            if (!made.has_value())
                return ll::makeStringError(made.error().message());

            std::string insert = "INSERT OR REPLACE INTO \"" + mSide + "\"(id";
            for (std::size_t i = 0; i < N; ++i)
                insert += "," + quoted(i);
            insert += ") VALUES(?";
            for (std::size_t i = 0; i < N; ++i)
                insert += ",?";
            insert += ")";

            auto rows = mRepo.store().records(mRoot, kTypedRowKind, 0);
            if (!rows.has_value())
                return ll::makeStringError(rows.error().message());
            for (auto const& rec : rows.value()) {
                if (rec.state == BlockLifecycle::Deleted)
                    continue;
                std::vector<BlockProp> params;
                params.reserve(N + 1);
                BlockProp idParam;
                idParam.type = PayloadType::Int;
                idParam.intValue = rec.id;
                params.push_back(idParam);
                RowCells cells = cellsOf(rec);
                bool any = false;
                for (std::size_t i = 0; i < N; ++i) {
                    auto it = cells.find(static_cast<PropKey>(i));
                    BlockProp v;
                    v.key = static_cast<PropKey>(i);
                    if (it == cells.end()) {
                        v.type = PayloadType::Null;
                        params.push_back(v);
                        continue;
                    }
                    any = true;
                    params.push_back(serializeCell(it->second, kAffinity[i]));
                }
                if (!any)
                    continue;
                auto r = (*b)->execCells("sideBackfill:" + mSide, insert, params);
                if (!r.has_value())
                    return ll::makeStringError(r.error().message());
            }

            auto c = (*b)->commit();
            if (!c.has_value())
                return ll::makeStringError(c.error().message());
            auto w = mRepo.metaSet("sidecol:" + mName, want);
            if (!w.has_value())
                return ll::makeStringError(w.error().message());
            return {};
        }

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

        ll::Expected<void> mirrorProps(BlockId id, RowCells const& cells) {
            for (auto const& [k, p] : cells) {
                if (!indexedAt(k))
                    continue;
                auto tv = cellToString(p);
                auto r = mRepo.store().setProp(id, k, p.type, p.intValue, p.realValue, tv);
                if (!r.has_value())
                    return ll::makeStringError(r.error().message());
            }
            return {};
        }

        ll::Expected<void> backfillIndexes() {
            auto ch = mRepo.store().children(mRoot, -1);
            if (!ch.has_value())
                return ll::makeStringError(ch.error().message());
            auto b = WriteBatch::begin(mRepo.store());
            if (!b.has_value())
                return ll::makeStringError(b.error().message());
            for (auto id : ch.value()) {
                auto rec = mRepo.store().load(id);
                if (!rec.has_value() || rec.value().state == BlockLifecycle::Deleted)
                    continue;
                auto cells = cellsOf(rec.value());
                for (auto const& [k, p] : cells) {
                    if (!indexedAt(k))
                        continue;
                    auto tv = cellToString(p);
                    auto r = (*b)->setProp(id, k, p.type, p.intValue, p.realValue, tv);
                    if (!r.has_value())
                        return ll::makeStringError(r.error().message());
                }
            }
            auto c = (*b)->commit();
            if (!c.has_value())
                return ll::makeStringError(c.error().message());
            return {};
        }

        explicit TypedTable(BlockRepository& repo, std::string name, BlockId root)
            : mRepo(repo), mName(std::move(name)), mRoot(root), mSide(sideNameOf(root)) {}

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
            std::string mask = indexMask();
            std::string want = std::to_string(Version);
            want.push_back('\x1e');
            want += std::string(magic_enum::enum_type_name<E>());
            want.push_back('\x1e');
            want += std::to_string(N);
            want.push_back('\x1e');
            want += mask;

            bool needBackfill = false;
            auto stored = repo.metaGet(metaKey);
            if (!stored.has_value())
                return ll::makeStringError(stored.error().message());
            if (stored.value().has_value() && !stored.value()->empty()) {
                std::string_view s = *stored.value();
                auto a = s.find('\x1e');
                auto b = (a == std::string_view::npos) ? std::string_view::npos : s.find('\x1e', a + 1);
                auto c = (b == std::string_view::npos) ? std::string_view::npos : s.find('\x1e', b + 1);
                std::string_view storedN = (c == std::string_view::npos)
                    ? s.substr(b + 1)
                    : s.substr(b + 1, c - b - 1);
                std::string_view storedMask = (c == std::string_view::npos)
                    ? std::string_view{}
                    : s.substr(c + 1);
                if (a == std::string_view::npos || b == std::string_view::npos
                    || s.substr(0, a) != std::to_string(Version)
                    || s.substr(a + 1, b - a - 1) != magic_enum::enum_type_name<E>()
                    || storedN != std::to_string(N))
                    return ll::makeErrorCodeError(
                        BlockError::makeErrorCode(BlockError::BlockErrorCode::SchemaMismatch));
                needBackfill = (storedMask != mask);
            } else {
                auto w = repo.metaSet(metaKey, want);
                if (!w.has_value())
                    return ll::makeStringError(w.error().message());
            }

            TypedTable table(repo, std::string(name), rootId);
            if (needBackfill) {
                auto bf = table.backfillIndexes();
                if (!bf.has_value())
                    return ll::makeStringError(bf.error().message());
                auto w = repo.metaSet(metaKey, want);
                if (!w.has_value())
                    return ll::makeStringError(w.error().message());
            }
            auto side = table.ensureSide();
            if (!side.has_value())
                return ll::makeStringError(side.error().message());
            return table;
        }

        template <class T>
        [[nodiscard]] ll::Expected<void> set(std::string_view rowKey, Key col, T const& v) {
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());

            auto cell = makeCell(colKey(col.value), v);
            if (!cell.has_value())
                return ll::makeStringError(cell.error().message());
            std::size_t slot = static_cast<std::size_t>(colKey(col.value));

            auto found = mRepo.store().idOf(rid.value(), rowKey);
            if (!found.has_value())
                return ll::makeStringError(found.error().message());
            if (found.value().has_value())
                return writeCell(*found.value(), slot, cell.value());

            auto b = WriteBatch::begin(mRepo.store());
            if (!b.has_value())
                return ll::makeStringError(b.error().message());
            auto made = (*b)->upsertRow(rid.value(), rowKey);
            if (!made.has_value())
                return ll::makeStringError(made.error().message());
            BlockProp idParam;
            idParam.type = PayloadType::Int;
            idParam.intValue = *made;
            BlockProp value = serializeCell(cell.value(), kAffinity[slot]);
            BlockProp params[3] = {idParam, value, value};
            auto r = (*b)->execCells(cellKey(slot), cellSql(slot), params);
            if (!r.has_value())
                return ll::makeStringError(r.error().message());
            auto c = (*b)->commit();
            if (!c.has_value())
                return ll::makeStringError(c.error().message());
            return {};
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

            auto id = mRepo.store().idOf(rid.value(), rowKey);
            if (!id.has_value())
                return ll::makeStringError(id.error().message());
            if (!id.value().has_value())
                return def;

            std::size_t slot = static_cast<std::size_t>(colKey(col.value));
            std::optional<BlockProp> got;
            BlockProp idParam;
            idParam.type = PayloadType::Int;
            idParam.intValue = *id.value();
            auto queried = mRepo.store().withQuery(
                "sideGet:" + mSide + ":" + std::to_string(slot),
                "SELECT " + quoted(slot) + " FROM \"" + mSide + "\" WHERE id=?",
                std::span<const BlockProp>(&idParam, 1),
                [&](SQLite::Statement& stmt) { got = readColumn(stmt, 0, colKey(col.value), kAffinity[slot]); });
            if (!queried.has_value())
                return ll::makeStringError(queried.error().message());

            if (got.has_value()) {
                auto v = readCell<T>(*got);
                if (!v.has_value())
                    return ll::makeStringError(v.error().message());
                return std::move(v.value());
            }

            RowCells cells;
            bool fromPayload = false;
            auto viewed = mRepo.store().withBlock(*id.value(), [&](BlockView const& view) {
                if (view.payload.empty())
                    return;
                cells = decodeRow(view.payload);
                fromPayload = true;
            });
            if (!viewed.has_value())
                return ll::makeStringError(viewed.error().message());
            if (!fromPayload) {
                auto rec = mRepo.store().load(*id.value());
                if (!rec.has_value() || rec.value().state == BlockLifecycle::Deleted)
                    return def;
                cells = cellsOf(rec.value());
            }

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
            auto id = mRepo.store().idOf(rid.value(), rowKey);
            if (!id.has_value())
                return ll::makeStringError(id.error().message());
            if (!id.value().has_value())
                return {};
            return mRepo.store().control(*id.value(), BlockLifecycle::Deleted);
        }

        [[nodiscard]] ll::Expected<bool> has(std::string_view rowKey) {
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            auto id = mRepo.store().idOf(rid.value(), rowKey);
            if (!id.has_value())
                return ll::makeStringError(id.error().message());
            return id.value().has_value();
        }

        [[nodiscard]] ll::Expected<std::vector<std::string>> list() {
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            auto ch = mRepo.store().childNames(rid.value());
            if (!ch.has_value())
                return ll::makeStringError(ch.error().message());
            std::vector<std::string> keys;
            keys.reserve(ch.value().size());
            for (auto const& entry : ch.value())
                keys.emplace_back(entry.second);
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

        static bool matchesAll(RowCells const& cells, std::vector<std::pair<Key, std::string>> const& conds) {
            for (auto const& [col, val] : conds) {
                auto it = cells.find(colKey(col.value));
                if (it == cells.end() || cellToString(it->second) != val)
                    return false;
            }
            return true;
        }

        static bool matchesAny(RowCells const& cells, std::vector<std::pair<Key, std::string>> const& conds) {
            for (auto const& [col, val] : conds) {
                auto it = cells.find(colKey(col.value));
                if (it != cells.end() && cellToString(it->second) == val)
                    return true;
            }
            return false;
        }

        [[nodiscard]] ll::Expected<std::vector<std::string>> find(
            FindMode mode, std::vector<std::pair<Key, std::string>> const& conds) {
            auto rid = root();
            if (!rid.has_value())
                return ll::makeStringError(rid.error().message());
            if (conds.empty())
                return list();

            std::string sql = "SELECT b.name FROM \"" + mSide
                            + "\" s JOIN block b ON b.id = s.id WHERE b.state < 8 AND (";
            std::vector<BlockProp> params;
            params.reserve(conds.size());
            for (std::size_t i = 0; i < conds.size(); ++i) {
                auto const& [col, val] = conds[i];
                std::size_t slot = static_cast<std::size_t>(colKey(col.value));
                if (i)
                    sql += (mode == FindMode::Or ? " OR " : " AND ");
                sql += "s." + quoted(slot);
                sql += " = ?";
                params.push_back(condProp(slot, val));
            }
            sql += ")";

            for (auto const& [col, val] : conds)
                ensureColumnIndex(static_cast<std::size_t>(colKey(col.value)));

            std::vector<std::string> out;
            std::string fkey = "sideFind:" + mSide + (mode == FindMode::And ? ":A:" : ":O:");
            for (auto const& [col, val] : conds)
                fkey += std::to_string(colKey(col.value)) + ",";
            auto r = mRepo.store().withQuery(
                fkey, sql, params,
                [&](SQLite::Statement& stmt) { out.emplace_back(stmt.getColumn(0).getString()); });
            if (!r.has_value())
                return ll::makeStringError(r.error().message());
            return out;
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
            auto id = mRepo.store().idOf(rid.value(), rowKey);
            if (!id.has_value())
                return ll::makeStringError(id.error().message());
            std::unordered_map<std::string, std::string> out;
            if (!id.value().has_value())
                return out;
            auto cells = readSideCells(*id.value());
            if (!cells.has_value())
                return ll::makeStringError(cells.error().message());
            auto names = magic_enum::enum_names<E>();
            for (auto const& [k, p] : cells.value()) {
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
            TypedTable& mOwner;
            BlockRepository& mRepo;
            std::unique_ptr<WriteBatch> mTx;
            BlockId mRoot;
            std::unordered_map<std::string, RowCells> mPending;
            std::unordered_set<std::string> mDeleted;

        public:
            Batch(TypedTable& owner, BlockRepository& repo, std::unique_ptr<WriteBatch> tx, BlockId root)
                : mOwner(owner), mRepo(repo), mTx(std::move(tx)), mRoot(root) {}

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
                auto id = mRepo.store().idOf(mRoot, rowKey);
                if (!id.has_value())
                    return ll::makeStringError(id.error().message());
                if (!id.value().has_value())
                    return def;
                auto cells = mOwner.readSideCells(*id.value());
                if (!cells.has_value())
                    return ll::makeStringError(cells.error().message());
                auto it = cells.value().find(colKey(col.value));
                if (it == cells.value().end())
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
                    auto rid = mTx->upsertRow(mRoot, rowKey);
                    if (!rid.has_value())
                        return ll::makeStringError(rid.error().message());
                    BlockId id = *rid;

                    RowCells merged = std::move(cells);
                    if (merged.size() < N) {
                        auto existing = mOwner.readSideCells(id);
                        if (existing.has_value())
                            for (auto const& [k, p] : existing.value())
                                if (!merged.contains(k))
                                    merged[k] = p;
                    }

                    std::string cols = "id";
                    std::vector<BlockProp> params;
                    BlockProp idParam;
                    idParam.type = PayloadType::Int;
                    idParam.intValue = id;
                    params.push_back(idParam);
                    for (auto const& [k, p] : merged) {
                        cols += "," + mOwner.quoted(static_cast<std::size_t>(k));
                        params.push_back(serializeCell(p, kAffinity[static_cast<std::size_t>(k)]));
                    }
                    std::string insert = "INSERT OR REPLACE INTO \"" + mOwner.mSide + "\"(" + cols + ") VALUES(?";
                    for (std::size_t i = 1; i < params.size(); ++i)
                        insert += ",?";
                    insert += ")";
                    auto pr = mTx->execCells("sideBatch:" + mOwner.mSide, insert, params);
                    if (!pr.has_value())
                        return ll::makeStringError(pr.error().message());
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
            return Batch(*this, mRepo, std::move(*b), rid.value());
        }
    };

}
