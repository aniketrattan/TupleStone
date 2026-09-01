// Out-of-line definitions for the frozen public API (ARCHITECTURE.md §12).
//
// M0 freezes the *surface*, not the behaviour: every entry point here links and
// returns Status::NotSupported so that the CLI, the harnesses, and downstream
// tests can be written against the real signatures today. The implementations
// arrive with M12; the accessors that must return a reference cannot return
// "not supported", so they abort — reaching them before M12 is a caller bug.
#include "nanosql/db.h"

#include "common/assert.h"

namespace nanosql {
namespace {

constexpr const char* kNotImplemented = "nanosql is not implemented yet (see PLAN.md, M12)";

}  // namespace

// --- ResultSet -------------------------------------------------------------

class ResultSet::Impl {};

ResultSet::ResultSet() = default;
ResultSet::ResultSet(ResultSet&&) noexcept = default;
ResultSet& ResultSet::operator=(ResultSet&&) noexcept = default;
ResultSet::~ResultSet() = default;

const Schema& ResultSet::schema() const { NANOSQL_UNREACHABLE(kNotImplemented); }
StatusOr<bool> ResultSet::Next() { return NotSupported(kNotImplemented); }
const Value& ResultSet::Get(int) const { NANOSQL_UNREACHABLE(kNotImplemented); }
const Value& ResultSet::Get(std::string_view) const { NANOSQL_UNREACHABLE(kNotImplemented); }
Status ResultSet::Close() { return Status::Ok(); }

// --- PreparedStatement -----------------------------------------------------

class PreparedStatement::Impl {};

PreparedStatement::PreparedStatement() = default;
PreparedStatement::PreparedStatement(PreparedStatement&&) noexcept = default;
PreparedStatement& PreparedStatement::operator=(PreparedStatement&&) noexcept = default;
PreparedStatement::~PreparedStatement() = default;

Status PreparedStatement::Bind(int, const Value&) { return NotSupported(kNotImplemented); }
StatusOr<ResultSet> PreparedStatement::Execute() { return NotSupported(kNotImplemented); }
Status PreparedStatement::Reset() { return NotSupported(kNotImplemented); }

// --- Transaction -----------------------------------------------------------

class Transaction::Impl {};

Transaction::Transaction() = default;
Transaction::Transaction(Transaction&&) noexcept = default;
Transaction& Transaction::operator=(Transaction&&) noexcept = default;
// Rolls back when neither Commit nor Rollback ran. Nothing to undo yet.
Transaction::~Transaction() = default;

Status Transaction::Commit() { return NotSupported(kNotImplemented); }
Status Transaction::Rollback() { return NotSupported(kNotImplemented); }

// --- Connection ------------------------------------------------------------

class Connection::Impl {};

Connection::Connection() = default;
Connection::~Connection() = default;

Status Connection::Execute(std::string_view) { return NotSupported(kNotImplemented); }
StatusOr<ResultSet> Connection::Query(std::string_view) { return NotSupported(kNotImplemented); }
StatusOr<PreparedStatement> Connection::Prepare(std::string_view) {
  return NotSupported(kNotImplemented);
}
StatusOr<Transaction> Connection::Begin() { return NotSupported(kNotImplemented); }

// --- Database --------------------------------------------------------------

class Database::Impl {};

Database::Database() = default;
Database::~Database() = default;

StatusOr<std::unique_ptr<Database>> Database::Open(std::string_view, const Options&) {
  return NotSupported(kNotImplemented);
}
StatusOr<std::unique_ptr<Connection>> Database::Connect() { return NotSupported(kNotImplemented); }
Status Database::Checkpoint() { return NotSupported(kNotImplemented); }
Status Database::Close() { return Status::Ok(); }

std::string VersionString() {
  return "nanosql " NANOSQL_VERSION;
}

}  // namespace nanosql
