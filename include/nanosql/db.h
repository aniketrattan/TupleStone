// The embedded API surface, frozen at M0 by ARCHITECTURE.md §12.
//
// Every declaration here is final: cli, api, and the test harnesses code against
// it. The definitions arrive with M12; until then the out-of-line methods return
// Status::NotSupported so that callers compile and link today.
//
// Threading (ARCHITECTURE.md §11): a Database is thread-safe and shared. A
// Connection is NOT thread-safe — one per thread — and PreparedStatement and
// ResultSet inherit that restriction from the Connection that made them.
#ifndef NANOSQL_DB_H_
#define NANOSQL_DB_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "nanosql/status.h"
#include "nanosql/value.h"

namespace nanosql {

struct Options {
  size_t buffer_pool_pages = 4096;      // 16 MiB at 4 KiB pages
  size_t sort_memory_bytes = 64u << 20;
  bool create_if_missing = true;
  bool sync_on_commit = true;           // false only for benchmarks; unsafe, and documented as such
};

class Connection;
class ResultSet;

// A streaming cursor. Holds its transaction open until closed or exhausted.
class ResultSet {
 public:
  ResultSet();
  ResultSet(ResultSet&&) noexcept;
  ResultSet& operator=(ResultSet&&) noexcept;
  ResultSet(const ResultSet&) = delete;
  ResultSet& operator=(const ResultSet&) = delete;
  ~ResultSet();

  const Schema& schema() const;
  StatusOr<bool> Next();  // false when exhausted
  const Value& Get(int column) const;
  const Value& Get(std::string_view column_name) const;
  Status Close();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class PreparedStatement {
 public:
  PreparedStatement();
  PreparedStatement(PreparedStatement&&) noexcept;
  PreparedStatement& operator=(PreparedStatement&&) noexcept;
  PreparedStatement(const PreparedStatement&) = delete;
  PreparedStatement& operator=(const PreparedStatement&) = delete;
  ~PreparedStatement();

  Status Bind(int index, const Value& v);
  StatusOr<ResultSet> Execute();
  Status Reset();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Rolls back in the destructor if neither Commit nor Rollback was called.
class Transaction {
 public:
  Transaction();
  Transaction(Transaction&&) noexcept;
  Transaction& operator=(Transaction&&) noexcept;
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  ~Transaction();

  Status Commit();
  Status Rollback();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class Connection {
 public:
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  ~Connection();

  Status Execute(std::string_view sql);  // no result rows
  StatusOr<ResultSet> Query(std::string_view sql);
  StatusOr<PreparedStatement> Prepare(std::string_view sql);
  StatusOr<Transaction> Begin();

 private:
  friend class Database;
  Connection();

  class Impl;
  std::unique_ptr<Impl> impl_;
};

class Database {
 public:
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  ~Database();

  static StatusOr<std::unique_ptr<Database>> Open(std::string_view path, const Options& = {});
  StatusOr<std::unique_ptr<Connection>> Connect();
  Status Checkpoint();
  Status Close();

 private:
  Database();

  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Build identification, so a bug report can name the binary it came from.
std::string VersionString();

}  // namespace nanosql

#endif  // NANOSQL_DB_H_
