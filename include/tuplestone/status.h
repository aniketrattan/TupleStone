// Public error-reporting surface. No tuplestone API ever throws; every fallible
// entry point returns Status or StatusOr<T>.
#ifndef TUPLESTONE_STATUS_H_
#define TUPLESTONE_STATUS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace tuplestone {

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
#define TUPLESTONE_DEFINE_STATUS_FACTORY(Name)                     \
  inline Status Name(std::string_view message) {                \
    return Status(StatusCode::k##Name, message);                \
  }                                                             \
  inline Status Name(std::string_view message, SourcePos pos) { \
    return Status(StatusCode::k##Name, message, pos);           \
  }

TUPLESTONE_DEFINE_STATUS_FACTORY(NotFound)
TUPLESTONE_DEFINE_STATUS_FACTORY(AlreadyExists)
TUPLESTONE_DEFINE_STATUS_FACTORY(InvalidArgument)
TUPLESTONE_DEFINE_STATUS_FACTORY(SyntaxError)
TUPLESTONE_DEFINE_STATUS_FACTORY(TypeError)
TUPLESTONE_DEFINE_STATUS_FACTORY(IoError)
TUPLESTONE_DEFINE_STATUS_FACTORY(Corruption)
TUPLESTONE_DEFINE_STATUS_FACTORY(Incompatible)
TUPLESTONE_DEFINE_STATUS_FACTORY(OutOfMemory)
TUPLESTONE_DEFINE_STATUS_FACTORY(OutOfRange)
TUPLESTONE_DEFINE_STATUS_FACTORY(SerializationFailure)
TUPLESTONE_DEFINE_STATUS_FACTORY(NotSupported)
TUPLESTONE_DEFINE_STATUS_FACTORY(Internal)

#undef TUPLESTONE_DEFINE_STATUS_FACTORY

// StatusOr<T> holds either a T or a non-Ok Status.
template <typename T>
class StatusOr {
 public:
  StatusOr(const Status& status) : status_(status) {}        // NOLINT(google-explicit-constructor)
  StatusOr(Status&& status) : status_(std::move(status)) {}  // NOLINT(google-explicit-constructor)
  StatusOr(const T& value) : value_(value) {}                // NOLINT(google-explicit-constructor)
  StatusOr(T&& value) : value_(std::move(value)) {}          // NOLINT(google-explicit-constructor)

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

// An Ok Status leaves a value-initialized T, which is useful for mutation APIs
// sharing a StatusOr return type with queries.
void StatusOrRejectOk();
#define TUPLESTONE_STATUSOR_REQUIRE_NOT_OK(s)       \
  do {                                           \
    if ((s).ok()) ::tuplestone::StatusOrRejectOk(); \
  } while (0)

// Early-return helpers. TUPLESTONE_RETURN_IF_ERROR(expr) propagates a failure;
// TUPLESTONE_ASSIGN_OR_RETURN(decl, expr) unwraps a StatusOr or propagates.
#define TUPLESTONE_RETURN_IF_ERROR(expr)          \
  do {                                         \
    ::tuplestone::Status _tuplestone_st = (expr);    \
    if (!_tuplestone_st.ok()) return _tuplestone_st; \
  } while (0)

#define TUPLESTONE_CONCAT_INNER(a, b) a##b
#define TUPLESTONE_CONCAT(a, b) TUPLESTONE_CONCAT_INNER(a, b)

#define TUPLESTONE_ASSIGN_OR_RETURN(decl, expr)                \
  auto TUPLESTONE_CONCAT(_tuplestone_or_, __LINE__) = (expr);     \
  if (!TUPLESTONE_CONCAT(_tuplestone_or_, __LINE__).ok())         \
    return TUPLESTONE_CONCAT(_tuplestone_or_, __LINE__).status(); \
  decl = std::move(TUPLESTONE_CONCAT(_tuplestone_or_, __LINE__)).value()

}  // namespace tuplestone

#endif  // TUPLESTONE_STATUS_H_
