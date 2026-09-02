// The public API is frozen at M0 and exercised here against the embedded engine.
#include "tuplestone/db.h"

#include <filesystem>
#include <memory>
#include <type_traits>

#include <gtest/gtest.h>

#include "tuplestone/status.h"
#include "tuplestone/value.h"

namespace tuplestone {
namespace {

TEST(ApiSurfaceTest, OptionsHaveTheDocumentedDefaults) {
  const Options options;
  EXPECT_EQ(options.buffer_pool_pages, 4096u);  // 16 MiB at 4 KiB pages
  EXPECT_EQ(options.sort_memory_bytes, 64u << 20);
  EXPECT_TRUE(options.create_if_missing);
  EXPECT_TRUE(options.sync_on_commit);
}

// A Connection is not thread-safe and must not be silently copied between
// threads; a Database is a shared handle and is likewise non-copyable.
TEST(ApiSurfaceTest, HandlesAreNonCopyable) {
  EXPECT_FALSE(std::is_copy_constructible_v<Database>);
  EXPECT_FALSE(std::is_copy_constructible_v<Connection>);
  EXPECT_FALSE(std::is_copy_constructible_v<Transaction>);
  EXPECT_FALSE(std::is_copy_constructible_v<PreparedStatement>);
  EXPECT_FALSE(std::is_copy_constructible_v<ResultSet>);
}

TEST(ApiSurfaceTest, ResultAndStatementHandlesAreMovable) {
  EXPECT_TRUE(std::is_move_constructible_v<ResultSet>);
  EXPECT_TRUE(std::is_move_constructible_v<PreparedStatement>);
  EXPECT_TRUE(std::is_move_constructible_v<Transaction>);
}

// Nothing in the public API may throw (ARCHITECTURE.md §8).
TEST(ApiSurfaceTest, OpenCreatesAnEmbeddedDatabase) {
  // Keep this smoke test independent of stale files left by an earlier build
  // (including databases written before a format/name change).
  const std::filesystem::path path = "tuplestone_api_surface.db";
  std::error_code error;
  std::filesystem::remove(path, error);
  std::filesystem::remove(path.string() + ".wal", error);
  const StatusOr<std::unique_ptr<Database>> db = Database::Open(path.string());
  ASSERT_TRUE(db.ok()) << db.status().ToString();
  EXPECT_TRUE((*db)->Close().ok());
  std::filesystem::remove(path, error);
  std::filesystem::remove(path.string() + ".wal", error);
}

TEST(ApiSurfaceTest, DefaultHandlesReportInvalidUse) {
  ResultSet result_set;
  EXPECT_EQ(result_set.Next().status().code(), StatusCode::kInvalidArgument);
  EXPECT_TRUE(result_set.Close().ok());

  PreparedStatement statement;
  EXPECT_EQ(statement.Execute().status().code(), StatusCode::kInvalidArgument);
  EXPECT_EQ(statement.Reset().code(), StatusCode::kInvalidArgument);

  Transaction txn;
  EXPECT_EQ(txn.Commit().code(), StatusCode::kInvalidArgument);
  EXPECT_TRUE(txn.Rollback().ok());
}

TEST(ApiSurfaceTest, VersionStringNamesTheProject) {
  const std::string version = VersionString();
  EXPECT_NE(version.find("tuplestone"), std::string::npos) << version;
}

}  // namespace
}  // namespace tuplestone
