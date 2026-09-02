#include "tuplestone/db.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

namespace tuplestone {
namespace {

TEST(EngineTest, PersistsTypedRowsAndQueries) {
  const std::string path = "engine_test.db";
  std::remove(path.c_str());
  std::remove((path + ".wal").c_str());
  {
    auto database = Database::Open(path);
    ASSERT_TRUE(database.ok()) << database.status().ToString();
    auto connection = (*database)->Connect();
    ASSERT_TRUE(connection.ok());
    ASSERT_TRUE(
        (*connection)
            ->Execute(
                "CREATE TABLE people (id INTEGER PRIMARY KEY, name TEXT NOT NULL, score REAL)")
            .ok());
    ASSERT_TRUE(
        (*connection)->Execute("INSERT INTO people VALUES (1, 'Ada', 9.5), (2, 'Bob', NULL)").ok());
    auto result = (*connection)->Query("SELECT id, name FROM people WHERE score IS NULL");
    ASSERT_TRUE(result.ok()) << result.status().ToString();
    ASSERT_TRUE(result->Next().value());
    EXPECT_EQ(result->Get(0).AsInteger(), 2);
    EXPECT_EQ(result->Get(1).AsText(), "Bob");
  }
  auto reopened = Database::Open(path, Options{.create_if_missing = false});
  ASSERT_TRUE(reopened.ok()) << reopened.status().ToString();
  auto connection = (*reopened)->Connect();
  ASSERT_TRUE(connection.ok());
  auto result = (*connection)->Query("SELECT COUNT(*) FROM people");
  ASSERT_TRUE(result.ok()) << result.status().ToString();
  ASSERT_TRUE(result->Next().value());
  EXPECT_EQ(result->Get(0).AsInteger(), 2);
  auto rows = (*connection)->Query("SELECT id FROM people ORDER BY id");
  ASSERT_TRUE(rows.ok());
  ASSERT_TRUE(rows->Next().value());
  EXPECT_EQ(rows->Get(0).AsInteger(), 1);
  ASSERT_TRUE(rows->Next().value());
  EXPECT_EQ(rows->Get(0).AsInteger(), 2);
  std::remove(path.c_str());
  std::remove((path + ".wal").c_str());
}

TEST(EngineTest, PreparedParametersAndRollback) {
  const std::string path = "engine_txn_test.db";
  std::remove(path.c_str());
  std::remove((path + ".wal").c_str());
  auto database = Database::Open(path);
  ASSERT_TRUE(database.ok());
  auto connection = (*database)->Connect();
  ASSERT_TRUE(
      (*connection)->Execute("CREATE TABLE items (id INTEGER PRIMARY KEY, label TEXT)").ok());
  auto statement = (*connection)->Prepare("INSERT INTO items VALUES (?, ?)");
  ASSERT_TRUE(statement.ok());
  ASSERT_TRUE(statement->Bind(1, Value(7)).ok());
  ASSERT_TRUE(statement->Bind(2, Value("seven")).ok());
  ASSERT_TRUE(statement->Execute().ok());
  ASSERT_TRUE((*connection)->Execute("BEGIN").ok());
  ASSERT_TRUE((*connection)->Execute("INSERT INTO items VALUES (8, 'eight')").ok());
  ASSERT_TRUE((*connection)->Execute("ROLLBACK").ok());
  auto select = (*connection)->Prepare("SELECT id FROM items WHERE id = ?");
  ASSERT_TRUE(select.ok());
  ASSERT_TRUE(select->Bind(1, Value(7)).ok());
  auto selected = select->Execute();
  ASSERT_TRUE(selected.ok()) << selected.status().ToString();
  ASSERT_TRUE(selected->Next().value());
  EXPECT_EQ(selected->Get(0).AsInteger(), 7);
  auto result = (*connection)->Query("SELECT id FROM items");
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->Next().value());
  EXPECT_EQ(result->Get(0).AsInteger(), 7);
  EXPECT_FALSE(result->Next().value());
  std::remove(path.c_str());
  std::remove((path + ".wal").c_str());
}

TEST(EngineTest, ReplaysCommittedSnapshotWhenDataFileIsLost) {
  const std::string path = "engine_recovery_test.db";
  const std::string wal_path = path + ".wal";
  std::remove(path.c_str());
  std::remove(wal_path.c_str());
  {
    auto database = Database::Open(path);
    ASSERT_TRUE(database.ok()) << database.status().ToString();
    auto connection = (*database)->Connect();
    ASSERT_TRUE(connection.ok());
    ASSERT_TRUE(
        (*connection)->Execute("CREATE TABLE recovery (id INTEGER PRIMARY KEY, note TEXT)").ok());
    ASSERT_TRUE((*connection)->Execute("INSERT INTO recovery VALUES (1, 'durable')").ok());
    EXPECT_GT(std::filesystem::file_size(wal_path), static_cast<uintmax_t>(0));
    ASSERT_TRUE((*database)->Close().ok());
  }

  // Simulate a crash after the commit record was durable but before the data
  // file became available to the next process.
  ASSERT_EQ(std::remove(path.c_str()), 0);
  auto reopened = Database::Open(path);
  ASSERT_TRUE(reopened.ok()) << reopened.status().ToString();
  auto connection = (*reopened)->Connect();
  ASSERT_TRUE(connection.ok());
  auto result = (*connection)->Query("SELECT note FROM recovery WHERE id = 1");
  ASSERT_TRUE(result.ok()) << result.status().ToString();
  ASSERT_TRUE(result->Next().value());
  EXPECT_EQ(result->Get(0).AsText(), "durable");
  EXPECT_FALSE(result->Next().value());
  (void)(*reopened)->Close();
  std::remove(path.c_str());
  std::remove(wal_path.c_str());
}

TEST(EngineTest, UpdatesAndDeletesPreserveConstraints) {
  const std::string path = "engine_update_test.db";
  std::remove(path.c_str());
  std::remove((path + ".wal").c_str());
  auto database = Database::Open(path);
  ASSERT_TRUE(database.ok());
  auto connection = (*database)->Connect();
  ASSERT_TRUE((*connection)
                  ->Execute("CREATE TABLE tasks (id INTEGER PRIMARY KEY, state TEXT NOT NULL, "
                            "priority INTEGER UNIQUE)")
                  .ok());
  EXPECT_EQ((*connection)->Execute("CREATE INDEX task_state ON tasks (state)").code(),
            StatusCode::kNotSupported);
  ASSERT_TRUE(
      (*connection)->Execute("INSERT INTO tasks VALUES (1, 'open', 10), (2, 'open', 20)").ok());
  ASSERT_TRUE((*connection)->Execute("UPDATE tasks SET state = 'done' WHERE id = 1").ok());
  auto like = (*connection)->Query("SELECT id FROM tasks WHERE state LIKE 'd%'");
  ASSERT_TRUE(like.ok());
  ASSERT_TRUE(like->Next().value());
  EXPECT_EQ(like->Get(0).AsInteger(), 1);
  EXPECT_EQ((*connection)->Execute("UPDATE tasks SET priority = 20 WHERE id = 1").code(),
            StatusCode::kAlreadyExists);
  ASSERT_TRUE((*connection)->Execute("DELETE FROM tasks WHERE id = 2").ok());
  auto result = (*connection)->Query("SELECT state, priority FROM tasks WHERE id = 1");
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->Next().value());
  EXPECT_EQ(result->Get(0).AsText(), "done");
  EXPECT_EQ(result->Get(1).AsInteger(), 10);
  (void)(*database)->Close();
  std::remove(path.c_str());
  std::remove((path + ".wal").c_str());
}

}  // namespace
}  // namespace tuplestone
