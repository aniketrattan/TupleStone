// The public API is frozen at M0 (ARCHITECTURE.md §12) but implemented at M12.
// This test pins the surface: it will not compile if a signature drifts, and it
// asserts that every unimplemented entry point reports NotSupported rather than
// pretending to succeed.
#include "nanosql/db.h"

#include <memory>
#include <type_traits>

#include <gtest/gtest.h>

#include "nanosql/status.h"
#include "nanosql/value.h"

namespace nanosql {
namespace {

TEST(ApiSurfaceTest, OptionsHaveTheDocumentedDefaults) {
  const Options options;
  EXPECT_EQ(options.buffer_pool_pages, 4096u);   // 16 MiB at 4 KiB pages
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
TEST(ApiSurfaceTest, OpenReportsNotSupportedRatherThanThrowing) {
  const StatusOr<std::unique_ptr<Database>> db = Database::Open("scratch.db");
  ASSERT_FALSE(db.ok());
  EXPECT_EQ(db.status().code(), StatusCode::kNotSupported);
}

TEST(ApiSurfaceTest, UnimplementedEntryPointsAllReportNotSupported) {
  ResultSet result_set;
  EXPECT_EQ(result_set.Next().status().code(), StatusCode::kNotSupported);
  EXPECT_TRUE(result_set.Close().ok());

  PreparedStatement statement;
  EXPECT_EQ(statement.Execute().status().code(), StatusCode::kNotSupported);
  EXPECT_EQ(statement.Reset().code(), StatusCode::kNotSupported);

  Transaction txn;
  EXPECT_EQ(txn.Commit().code(), StatusCode::kNotSupported);
  EXPECT_EQ(txn.Rollback().code(), StatusCode::kNotSupported);
}

TEST(ApiSurfaceTest, VersionStringNamesTheProject) {
  const std::string version = VersionString();
  EXPECT_NE(version.find("nanosql"), std::string::npos) << version;
}

}  // namespace
}  // namespace nanosql
