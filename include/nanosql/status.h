// Public error-reporting surface. No nanosql API ever throws; every fallible
// entry point returns Status or StatusOr<T>.
#ifndef NANOSQL_STATUS_H_
#define NANOSQL_STATUS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace nanosql {

// ARCHITECTURE.md §8. Ok must be 0 so that `if (code)` reads as "failed".
enum class StatusCode : uint8_t {
  kOk = 0,
  kNotFound,
  kAlreadyExists,
  kInvalidArgument,
  kSyntaxError,
  kTypeError,
  kIoError,
  kCorruption,
  kIncompatible,
  kOutOfMemory,
  kOutOfRange,
  kSerializationFailure,
  kNotSupported,
  kInternal,
};

const char* StatusCodeName(StatusCode code);

// A source position inside a SQL statement. Both are 1-based; line == 0 means
// "no position", which is the case for every non-SQL error.
struct SourcePos {
  uint32_t line = 0;
  uint32_t column = 0;

  bool valid() const { return line != 0; }
};

// Status is the size of one pointer when Ok, which is the overwhelmingly common
// case: the message and position live in a heap payload allocated only on error.
class Status {
 public:
  Status() = default;
  Status(StatusCode code, std::string_view message);
  Status(StatusCode code, std::string_view message, SourcePos pos);

  Status(const Status& other);
  Status& operator=(const Status& other);
  Status(Status&&) noexcept = default;
  Status& operator=(Status&&) noexcept = default;
  ~Status() = default;

  static Status Ok() { return Status(); }

  bool ok() const { return payload_ == nullptr; }
  StatusCode code() const { return payload_ ? payload_->code : StatusCode::kOk; }
  std::string_view message() const { return payload_ ? std::string_view(payload_->message) : ""; }
  SourcePos pos() const { return payload_ ? payload_->pos : SourcePos{}; }

  bool operator==(const Status& other) const;
  bool operator!=(const Status& other) const { return !(*this == other); }

  // "Ok", or "Corruption: bad page checksum", or "SyntaxError: line 1:23: ...".
  std::string ToString() const;

 private:
  struct Payload {
    StatusCode code;
    SourcePos pos;
    std::string message;
  };

  std::unique_ptr<Payload> payload_;
};

// Named constructors. These read better at call sites than Status(kFoo, "...").
#define NANOSQL_DEFINE_STATUS_FACTORY(Name)                                            \
  inline Status Name(std::string_view message) {                                       \
    return Status(StatusCode::k##Name, message);                                       \
  }                                                                                    \
  inline Status Name(std::string_view message, SourcePos pos) {                         \
    return Status(StatusCode::k##Name, message, pos);                                   \
  }

NANOSQL_DEFINE_STATUS_FACTORY(NotFound)
NANOSQL_DEFINE_STATUS_FACTORY(AlreadyExists)
NANOSQL_DEFINE_STATUS_FACTORY(InvalidArgument)
NANOSQL_DEFINE_STATUS_FACTORY(SyntaxError)
NANOSQL_DEFINE_STATUS_FACTORY(TypeError)
NANOSQL_DEFINE_STATUS_FACTORY(IoError)
NANOSQL_DEFINE_STATUS_FACTORY(Corruption)
NANOSQL_DEFINE_STATUS_FACTORY(Incompatible)
NANOSQL_DEFINE_STATUS_FACTORY(OutOfMemory)
NANOSQL_DEFINE_STATUS_FACTORY(OutOfRange)
NANOSQL_DEFINE_STATUS_FACTORY(SerializationFailure)
NANOSQL_DEFINE_STATUS_FACTORY(NotSupported)
NANOSQL_DEFINE_STATUS_FACTORY(Internal)

#undef NANOSQL_DEFINE_STATUS_FACTORY

// Returning an Ok Status through StatusOr would leave it with no value; that is
// always a bug at the call site, so it aborts rather than returning garbage.
void StatusOrRejectOk();
#define NANOSQL_STATUSOR_REQUIRE_NOT_OK(s)   do {                                         if ((s).ok()) ::nanosql::StatusOrRejectOk();   } while (0)

// StatusOr<T> holds either a T or a non-Ok Status, never both and never neither.
template <typename T>
class StatusOr {
 public:
  StatusOr(const Status& status) : status_(status) {  // NOLINT(google-explicit-constructor)
    NANOSQL_STATUSOR_REQUIRE_NOT_OK(status_);
  }
  StatusOr(Status&& status) : status_(std::move(status)) {  // NOLINT(google-explicit-constructor)
    NANOSQL_STATUSOR_REQUIRE_NOT_OK(status_);
  }
  StatusOr(const T& value) : value_(value) {}       // NOLINT(google-explicit-constructor)
  StatusOr(T&& value) : value_(std::move(value)) {} // NOLINT(google-explicit-constructor)

  bool ok() const { return status_.ok(); }
  const Status& status() const { return status_; }

  // Precondition: ok(). Calling these on an error StatusOr is a logic bug.
  const T& value() const& { return value_; }
  T& value() & { return value_; }
  T&& value() && { return std::move(value_); }

  const T& operator*() const& { return value_; }
  T& operator*() & { return value_; }
  T&& operator*() && { return std::move(value_); }
  const T* operator->() const { return &value_; }
  T* operator->() { return &value_; }

 private:
  Status status_;
  T value_{};
};

// Returning an Ok Status through StatusOr would leave it with no value; that is
// always a bug at the call site, so it aborts rather than returning garbage.
void StatusOrRejectOk();
#define NANOSQL_STATUSOR_REQUIRE_NOT_OK(s) \
  do {                                     \
    if ((s).ok()) ::nanosql::StatusOrRejectOk(); \
  } while (0)

// Early-return helpers. NANOSQL_RETURN_IF_ERROR(expr) propagates a failure;
// NANOSQL_ASSIGN_OR_RETURN(decl, expr) unwraps a StatusOr or propagates.
#define NANOSQL_RETURN_IF_ERROR(expr)             \
  do {                                            \
    ::nanosql::Status _nanosql_st = (expr);       \
    if (!_nanosql_st.ok()) return _nanosql_st;    \
  } while (0)

#define NANOSQL_CONCAT_INNER(a, b) a##b
#define NANOSQL_CONCAT(a, b) NANOSQL_CONCAT_INNER(a, b)

#define NANOSQL_ASSIGN_OR_RETURN(decl, expr)                          \
  auto NANOSQL_CONCAT(_nanosql_or_, __LINE__) = (expr);               \
  if (!NANOSQL_CONCAT(_nanosql_or_, __LINE__).ok())                   \
    return NANOSQL_CONCAT(_nanosql_or_, __LINE__).status();           \
  decl = std::move(NANOSQL_CONCAT(_nanosql_or_, __LINE__)).value()

}  // namespace nanosql

#endif  // NANOSQL_STATUS_H_
