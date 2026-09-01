#include "nanosql/status.h"

#include <cstdio>
#include <cstdlib>

namespace nanosql {

const char* StatusCodeName(StatusCode code) {
  switch (code) {
    case StatusCode::kOk: return "Ok";
    case StatusCode::kNotFound: return "NotFound";
    case StatusCode::kAlreadyExists: return "AlreadyExists";
    case StatusCode::kInvalidArgument: return "InvalidArgument";
    case StatusCode::kSyntaxError: return "SyntaxError";
    case StatusCode::kTypeError: return "TypeError";
    case StatusCode::kIoError: return "IoError";
    case StatusCode::kCorruption: return "Corruption";
    case StatusCode::kIncompatible: return "Incompatible";
    case StatusCode::kOutOfMemory: return "OutOfMemory";
    case StatusCode::kOutOfRange: return "OutOfRange";
    case StatusCode::kSerializationFailure: return "SerializationFailure";
    case StatusCode::kNotSupported: return "NotSupported";
    case StatusCode::kInternal: return "Internal";
  }
  return "Unknown";
}

Status::Status(StatusCode code, std::string_view message)
    : Status(code, message, SourcePos{}) {}

Status::Status(StatusCode code, std::string_view message, SourcePos pos) {
  // Constructing an Ok Status with a message would produce an object whose
  // ok() disagrees with its contents. Silently dropping the message is the
  // least surprising resolution; the alternative is an unrepresentable state.
  if (code == StatusCode::kOk) return;
  payload_ = std::make_unique<Payload>(Payload{code, pos, std::string(message)});
}

Status::Status(const Status& other) {
  if (other.payload_ != nullptr) {
    payload_ = std::make_unique<Payload>(*other.payload_);
  }
}

Status& Status::operator=(const Status& other) {
  if (this != &other) {
    payload_ = other.payload_ != nullptr ? std::make_unique<Payload>(*other.payload_) : nullptr;
  }
  return *this;
}

bool Status::operator==(const Status& other) const {
  if (payload_ == nullptr || other.payload_ == nullptr) {
    return payload_ == other.payload_;
  }
  return payload_->code == other.payload_->code && payload_->message == other.payload_->message &&
         payload_->pos.line == other.payload_->pos.line &&
         payload_->pos.column == other.payload_->pos.column;
}

std::string Status::ToString() const {
  if (payload_ == nullptr) return "Ok";
  std::string out = StatusCodeName(payload_->code);
  out += ": ";
  if (payload_->pos.valid()) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "line %u:%u: ", payload_->pos.line, payload_->pos.column);
    out += buf;
  }
  out += payload_->message;
  return out;
}

void StatusOrRejectOk() {
  std::fflush(stdout);
  std::fprintf(stderr, "\nnanosql: StatusOr<T> constructed from an Ok Status, which would leave "
                       "it with no value. This is a bug at the call site.\n");
  std::fflush(stderr);
  std::abort();
}

}  // namespace nanosql
