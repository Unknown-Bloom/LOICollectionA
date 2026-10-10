#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include <sqlite3.h>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/ColumnText.h"

TEST(ColumnTextTest, NullColumnYieldsEmptyString) {
    SQLite::Database db(":memory:", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    db.exec("CREATE TABLE t(v TEXT)");
    db.exec("INSERT INTO t VALUES(NULL)");

    SQLite::Statement query(db, "SELECT v FROM t");
    ASSERT_EQ(query.tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(columnToString(query.getColumn(0)), std::string{});
}

TEST(ColumnTextTest, EmptyTextColumnYieldsEmptyString) {
    SQLite::Database db(":memory:", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    db.exec("CREATE TABLE t(v TEXT)");
    db.exec("INSERT INTO t VALUES('')");

    SQLite::Statement query(db, "SELECT v FROM t");
    ASSERT_EQ(query.tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(columnToString(query.getColumn(0)), std::string{});
}

TEST(ColumnTextTest, PlainTextColumnSurvivesConversion) {
    SQLite::Database db(":memory:", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    db.exec("CREATE TABLE t(v TEXT)");
    db.exec("INSERT INTO t VALUES('players')");

    SQLite::Statement query(db, "SELECT v FROM t");
    ASSERT_EQ(query.tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(columnToString(query.getColumn(0)), "players");
}

TEST(ColumnTextTest, EmbeddedNulKeepsFullByteLength) {
    SQLite::Database db(":memory:", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    db.exec("CREATE TABLE t(v TEXT)");

    SQLite::Statement insert(db, "INSERT INTO t VALUES(?)");
    insert.bind(1, static_cast<void const*>("a\0b"), 3);
    ASSERT_EQ(insert.tryExecuteStep(), SQLITE_DONE);

    SQLite::Statement query(db, "SELECT v FROM t");
    ASSERT_EQ(query.tryExecuteStep(), SQLITE_ROW);

    auto text = columnToString(query.getColumn(0));
    ASSERT_EQ(text.size(), 3u);
    EXPECT_EQ(text, std::string_view("a\0b", 3));
}

TEST(ColumnTextTest, IntegerColumnIsReadAsText) {
    SQLite::Database db(":memory:", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    db.exec("CREATE TABLE t(v INTEGER)");
    db.exec("INSERT INTO t VALUES(42)");

    SQLite::Statement query(db, "SELECT v FROM t");
    ASSERT_EQ(query.tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(columnToString(query.getColumn(0)), "42");
}

TEST(ColumnTextTest, BlobColumnIsReadAsRawBytes) {
    SQLite::Database db(":memory:", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    db.exec("CREATE TABLE t(v BLOB)");

    SQLite::Statement insert(db, "INSERT INTO t VALUES(?)");
    insert.bind(1, static_cast<void const*>("\x01\x02\x00\x03"), 4);
    ASSERT_EQ(insert.tryExecuteStep(), SQLITE_DONE);

    SQLite::Statement query(db, "SELECT v FROM t");
    ASSERT_EQ(query.tryExecuteStep(), SQLITE_ROW);

    auto bytes = columnToString(query.getColumn(0));
    ASSERT_EQ(bytes.size(), 4u);
    EXPECT_EQ(bytes[0], '\x01');
    EXPECT_EQ(bytes[2], '\x00');
}

TEST(ColumnTextTest, Utf8ColumnSurvivesConversion) {
    SQLite::Database db(":memory:", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    db.exec("CREATE TABLE t(v TEXT)");
    db.exec("INSERT INTO t VALUES('玩家数据')");

    SQLite::Statement query(db, "SELECT v FROM t");
    ASSERT_EQ(query.tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(columnToString(query.getColumn(0)), std::string_view("玩家数据"));
}
