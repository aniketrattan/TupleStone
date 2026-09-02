// The embedded API surface, frozen at M0 by ARCHITECTURE.md §12.
//
// Every declaration here is final: the CLI, API, and test harnesses code against
// it. Implementations live in the API layer and remain compatible with this
// surface as TupleStone evolves.
//
// Threading (ARCHITECTURE.md §11): a Database is thread-safe and shared. A
// Connection is NOT thread-safe — one per thread — and PreparedStatement and
// ResultSet inherit that restriction from the Connection that made them.
#ifndef TUPLESTONE_DB_H_
#define TUPLESTONE_DB_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "tuplestone/status.h"
#include "tuplestone/value.h"

namespace tuplestone {

struct Options {
  size_t buffer_pool_pages = 4096;  // 16 MiB at 4 KiB pages
  size_t sort_memory_bytes = 64u << 20;
  bool create_if_missing = true;
  bool sync_on_commit = true;  // false only for benchmarks; unsafe, and documented as such
};

class Connection;
class ResultSet;

// A streaming cursor. Holds its transaction open until closed or exhausted.
class ResultSet {
 public:
  class Impl;
  ResultSet();
  // Internal materialization constructor used by the execution layer.
  ResultSet(const Impl& impl);
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

 public:
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

 public:
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

 public:
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

 public:
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

 public:
  Database();

  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Build identification, so a bug report can name the binary it came from.
std::string VersionString();

}  // namespace tuplestone

#endif  // TUPLESTONE_DB_H_
