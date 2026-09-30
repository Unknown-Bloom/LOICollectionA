#!/usr/bin/env python3
"""Migrate legacy SQLiteStorage KV databases into the TypedTable block format.

LOICollectionA historically stored every feature in its own SQLite file using a
generic key/value layer (``SQLiteStorage``): each table had a ``key`` primary key
plus ``created_at`` / ``updated_at`` bookkeeping columns and a set of domain
columns. PR #40 replaced that layer with a block model (``BlockStore`` /
``TypedTable``) whose physical schema is ``block`` / ``prop`` / ``link`` / ``meta``
/ ``dict`` and whose logical tables are written as a root block plus one child
block (``kind = 1000000000``) per row, with the row's columns mirrored into the
``prop`` table keyed by the TypedTable column index.

The original ``LegacyMigrator`` only archived the old file and never replayed the
rows, so legacy databases were silently treated as empty and their data was lost.
This script performs the replay that was missing: it reads every legacy KV table
and rewrites it into the block model in place, preserving all domain columns.

The schema is keyed by (database file, table name) because the legacy build kept
every feature in its own SQLite file and some plugins reused the same table name
with different column layouts -- for example every plugin kept a ``Blacklist``
table, but the blacklist plugin's copy has seven columns while the
tpa/chat/market copies have four. The mapping below is taken verbatim from the
TypedTable column enums in include/server/Plugins/types and from the legacy
``create("X", ...)`` column callbacks at the flat-layout commit, and must stay in
sync with them.

File routing follows the legacy layout exactly: tables that lived in a dedicated
file (blacklist/mute/tpa/chat/statistics/market) are migrated in place, while the
tables that the legacy build registered against the shared ``SettingsDB`` service
(Market, MarketTax, Language, Pvp, Chat, Tpa, Wallet and its siblings) all live in
``settings.db`` and are migrated there in place.

Two legacy tables are intentionally NOT migrated:
  * ``Notice`` -- the current build stores notices as JSON, not SQLite, so any
    leftover SQLite ``Notice`` table is left untouched on disk.
  * ``statistics.db/Language`` -- the legacy StatisticsPlugin cached player names
    here, but the current code reads the single ``Language`` table from
    ``settings.db`` (populated by LanguagePlugin). That copy is authoritative, so
    the statistics.db copy is left on disk rather than turned into an orphan root.

The application rebuilds the per-table side table (``col_<rootId>``) and the
``schema:`` / ``sidecol:`` meta fingerprints on first open, so this script only
has to create the base tables and the block/prop rows; it deliberately leaves the
meta fingerprints alone to avoid a SchemaMismatch on load. Integer/real/bool
columns are stored as TEXT in ``prop`` and converted back by the application's
cell codec and side-table backfill.

Usage:
    python migrate_block.py [--data-dir DIR] [--dry-run] [FILE ...]

With no FILE arguments the script scans the well-known database files
(blacklist.db, mute.db, tpa.db, chat.db, statistics.db, market.db, settings.db)
inside ``--data-dir`` (default: the current directory). Pass explicit paths to
limit the run.
"""

import argparse
import os
import sqlite3
import sys
import time

# Column layout of every TypedTable, keyed by the database file that the
# application opens it from, then by the on-disk table name. Each list is the
# TypedTable enum in order, copied from the matching *Schema.h and cross-checked
# against the legacy flat create("X", ...) column callbacks.
PER_FILE_SCHEMA = {
    "blacklist.db": {
        "Blacklist": ["name", "cause", "time", "subtime", "data_uuid", "data_ip", "data_clientid"],
    },
    "mute.db": {
        "Mute": ["name", "cause", "time", "subtime", "data"],
    },
    "tpa.db": {
        # legacy Blacklist table in tpa.db == TpaBlacklist (four columns)
        "Blacklist": ["name", "target", "author", "time"],
    },
    "chat.db": {
        # legacy Blacklist table in chat.db == ChatBlacklist (four columns)
        "Blacklist": ["name", "target", "author", "time"],
        "Titles": ["title", "author", "time"],
    },
    "statistics.db": {
        "Statistics": ["onlinetime", "kill", "death", "place", "destroy", "respawn", "joins"],
        # "Language" is intentionally absent: the current build reads the single
        # Language table from settings.db, so a statistics.db copy is left on disk.
    },
    "market.db": {
        # legacy Blacklist table in market.db == MarketBlacklist (four columns)
        "Blacklist": ["name", "target", "author", "time"],
        "Item": ["name", "icon", "introduce", "score", "data", "player_name", "player_uuid"],
        "Store": ["name", "introduce", "icon", "owner_uuid", "owner_name", "store_created_at"],
        "StoreItem": ["store_id", "name", "icon", "introduce", "score", "data"],
        "StoreSale": ["store_id", "item_name", "price", "tax", "buyer_uuid", "buyer_name",
                      "seller_uuid", "time", "source"],
        "StoreReview": ["store_id", "buyer_uuid", "buyer_name", "rating", "content", "status", "time"],
        "StoreWanted": ["wanted_uuid", "wanted_name", "item_type", "item_data", "item_name",
                        "unit_price", "amount_total", "amount_filled", "expire_at"],
        "StoreAuction": ["seller_uuid", "seller_name", "item_type", "item_data", "item_name",
                         "start_price", "current_price", "bidder_uuid", "bidder_name",
                         "bid_count", "end_at", "settled"],
    },
    "settings.db": {
        "Market": ["name", "score"],
        "MarketTax": ["total", "rate"],
        "Language": ["name", "value"],
        "Pvp": ["name", "enable"],
        "Chat": ["name", "title"],
        "Tpa": ["name", "invite"],
        "Wallet": ["name", "score", "balance"],
        "WalletFee": ["amount"],
        "WalletBank": ["principal", "deposit_at", "name"],
        "WalletLedger": ["from_uuid", "from_name", "to_uuid", "to_name", "amount", "fee",
                         "type", "time_ns", "time"],
        "RedEnvelope": ["chat_key", "sender_uuid", "sender_name", "capacity", "total",
                        "count", "people", "targets", "expire_at"],
        "RedEnvelopeGrab": ["name", "amount"],
        # "Notice" is intentionally absent: the current build stores notices as
        # JSON, not SQLite, so any leftover SQLite Notice table is left on disk.
    },
}

# Columns whose TypedTable affinity is bool: the legacy build stored them as the
# strings "true"/"false", which we normalise to "1"/"0" so both the side table
# (integer affinity) and the direct boolean reader agree.
BOOL_COLS = {
    ("Pvp", "enable"),
    ("Tpa", "invite"),
    ("StoreAuction", "settled"),
}

KIND_ROOT = 0
KIND_ROW = 1000000000
STATE_ACTIVE = 1
TYPE_TEXT = 3
MIGRATION_MARKER = "loi_migrate_flat_to_block_v1"

DEFAULT_DB_FILES = [
    "blacklist.db", "mute.db", "tpa.db", "chat.db",
    "statistics.db", "market.db", "settings.db",
]


def ensure_base_schema(conn):
    # Mirrors BlockStore::ensureSchema verbatim so the application's
    # "CREATE TABLE IF NOT EXISTS" becomes a clean no-op on first open.
    conn.executescript(
        """
        CREATE TABLE IF NOT EXISTS dict(
            id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE);
        CREATE TABLE IF NOT EXISTS block(
            id INTEGER PRIMARY KEY AUTOINCREMENT, parent INTEGER NOT NULL DEFAULT 0,
            name TEXT NOT NULL, kind INTEGER NOT NULL DEFAULT 0,
            state INTEGER NOT NULL DEFAULT 1, payload BLOB,
            created INTEGER NOT NULL, updated INTEGER NOT NULL);
        CREATE TABLE IF NOT EXISTS prop(
            block_id INTEGER NOT NULL, key INTEGER NOT NULL, type INTEGER NOT NULL,
            ival INTEGER NOT NULL DEFAULT 0, rval REAL NOT NULL DEFAULT 0, tval TEXT,
            PRIMARY KEY(block_id,key));
        CREATE TABLE IF NOT EXISTS link(
            src INTEGER NOT NULL, dst INTEGER NOT NULL, kind INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY(src,dst,kind));
        CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);
        """
    )


def table_names(conn):
    rows = conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"
    ).fetchall()
    return [r[0] for r in rows]


def column_names(conn, table):
    rows = conn.execute(f"PRAGMA table_info({quote_ident(table)})").fetchall()
    return [r[1] for r in rows]


def quote_ident(ident):
    return '"' + ident.replace('"', '""') + '"'


def is_old_old_format(conn):
    # Pre-flat layout used player-scoped tables named like "uuid$Feature".
    return any("$" in n for n in table_names(conn))


def already_migrated(conn):
    names = table_names(conn)
    if "meta" not in names:
        return False
    row = conn.execute(
        "SELECT value FROM meta WHERE key=?", (MIGRATION_MARKER,)
    ).fetchone()
    return row is not None


def get_root_id(conn, name):
    row = conn.execute(
        "SELECT id FROM block WHERE parent=0 AND name=?", (name,)
    ).fetchone()
    if row:
        return row[0]
    ts = now_ms()
    cur = conn.execute(
        "INSERT INTO block(parent,name,kind,state,payload,created,updated) "
        "VALUES(?,?,?,?,NULL,?,?)",
        (0, name, KIND_ROOT, STATE_ACTIVE, ts, ts),
    )
    return cur.lastrowid


def now_ms():
    return int(time.time() * 1000)


def normalize_value(table, column, value):
    if value is None:
        return None
    if (table, column) in BOOL_COLS:
        text = str(value).strip().lower()
        return "1" if text in ("1", "true", "yes", "y", "on") else "0"
    return str(value)


def migrate_table(conn, table, schema_cols):
    cols = [c.lower() for c in column_names(conn, table)]
    if "key" not in cols:
        return 0, 0
    key_idx = cols.index("key")
    index_of = {c: i for i, c in enumerate(schema_cols)}

    root_id = get_root_id(conn, table)
    ts = now_ms()
    rows = conn.execute(f"SELECT * FROM {quote_ident(table)}").fetchall()
    migrated = 0
    unmapped = set()
    for row in rows:
        key = row[key_idx]
        if key is None or str(key).strip() == "":
            continue
        cur = conn.execute(
            "INSERT INTO block(parent,name,kind,state,payload,created,updated) "
            "VALUES(?,?,?,?,NULL,?,?)",
            (root_id, str(key), KIND_ROW, STATE_ACTIVE, ts, ts),
        )
        block_id = cur.lastrowid
        for i, value in enumerate(row):
            if i == key_idx:
                continue
            col = cols[i]
            if col in ("created_at", "updated_at"):
                continue
            norm = normalize_value(table, col, value)
            if norm is None:
                continue
            idx = index_of.get(col)
            if idx is None:
                unmapped.add(col)
                continue
            conn.execute(
                "INSERT OR REPLACE INTO prop(block_id,key,type,ival,rval,tval) "
                "VALUES(?,?,?,0,0,?)",
                (block_id, idx, TYPE_TEXT, norm),
            )
        migrated += 1
    return migrated, len(unmapped)


def migrate_file(path, dry_run):
    if not os.path.exists(path):
        return f"skip   {path}: not found"
    basename = os.path.basename(path)
    schema = PER_FILE_SCHEMA.get(basename, {})
    conn = sqlite3.connect(path)
    try:
        conn.execute("PRAGMA foreign_keys = OFF")
        names = table_names(conn)

        if is_old_old_format(conn):
            conn.close()
            return (f"defer  {path}: pre-flat (uuid$ tables) detected; "
                    f"run migrate1100.py first, then this script")

        if already_migrated(conn):
            return f"skip   {path}: migration marker present"

        legacy_tables = [n for n in names if n in schema]
        if not legacy_tables:
            return f"skip   {path}: no legacy KV tables to migrate"

        if dry_run:
            conn.close()
            return f"would  {path}: migrate tables {legacy_tables}"

        # Archive the untouched database before mutating it.
        stamp = str(now_ms())
        archive_path = f"{path}.{stamp}.legacy"
        try:
            conn.execute(f"VACUUM INTO '{archive_path}'")
        except sqlite3.Error as exc:
            conn.close()
            return f"error  {path}: archive failed ({exc})"

        ensure_base_schema(conn)
        total_rows = 0
        unmapped_total = 0
        migrated_tables = []
        for table in legacy_tables:
            n, unmapped = migrate_table(conn, table, schema[table])
            if n:
                total_rows += n
                migrated_tables.append(f"{table}({n})")
                unmapped_total += unmapped

        # Drop the legacy KV tables now that their data lives in blocks.
        for table in migrated_tables:
            name = table.split("(")[0]
            conn.execute(f"DROP TABLE IF EXISTS {quote_ident(name)}")

        conn.execute(
            "INSERT OR REPLACE INTO meta(key,value) VALUES(?,?)",
            (MIGRATION_MARKER, str(now_ms())),
        )
        conn.commit()
        msg = f"ok     {path}: {total_rows} rows from {migrated_tables}"
        if unmapped_total:
            msg += f"  [{unmapped_total} unmapped cols kept in archive]"
        return msg
    finally:
        try:
            conn.close()
        except sqlite3.Error:
            pass


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data-dir", default=".",
                        help="directory containing the database files (default: .)")
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would be migrated without writing")
    parser.add_argument("files", nargs="*", help="explicit database paths to migrate")
    args = parser.parse_args(argv)

    if args.files:
        targets = args.files
    else:
        targets = [os.path.join(args.data_dir, f) for f in DEFAULT_DB_FILES]

    print("LOICollectionA flat->block migration")
    print(f"mode: {'dry-run' if args.dry_run else 'write'}")
    print("-" * 60)
    for path in targets:
        print(migrate_file(path, args.dry_run))
    print("-" * 60)
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
