// The SQL value model. The full Value type — three-valued logic, coercion rules,
// and the memcomparable key encoding — is built in M4 (PLAN.md). This header
// exists from M0 because db.h's public signatures name Value and Schema, and
// ARCHITECTURE.md §12 freezes that surface before anything implements it.
#ifndef TUPLESTONE_VALUE_H_
#define TUPLESTONE_VALUE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace tuplestone {

// ARCHITECTURE.md §9. The on-disk encoding of these discriminants is fixed by
// the catalog's `type` column, so the numeric values may not be reordered.
enum class TypeId : uint8_t {
  kNull = 0,
  kBoolean = 1,
  kInteger = 2,  // int64_t
  kReal = 3,     // double
  kText = 4,
  kBlob = 5,
};

const char* TypeIdName(TypeId type);

class Value {
 public:
  using Blob = std::vector<uint8_t>;

  Value() : value_(std::monostate{}) {}
  Value(std::nullptr_t) : value_(std::monostate{}) {}        // NOLINT(google-explicit-constructor)
  Value(bool value) : value_(value) {}                       // NOLINT(google-explicit-constructor)
  Value(int value) : value_(static_cast<int64_t>(value)) {}  // NOLINT(google-explicit-constructor)
  Value(int64_t value) : value_(value) {}                    // NOLINT(google-explicit-constructor)
  Value(double value) : value_(value) {}                     // NOLINT(google-explicit-constructor)
  Value(const char* value) : value_(std::string(value == nullptr ? "" : value)) {}  // NOLINT
  Value(std::string value) : value_(std::move(value)) {}  // NOLINT(google-explicit-constructor)
  Value(std::string_view value) : value_(std::string(value)) {}  // NOLINT
  Value(Blob value) : value_(std::move(value)) {}  // NOLINT(google-explicit-constructor)
  Value(const Value& other);
  Value& operator=(const Value& other);
  Value(Value&& other) noexcept = default;
  Value& operator=(Value&& other) noexcept = default;

  static Value Null() { return Value(); }
  static Value Boolean(bool value) { return Value(value); }
  static Value Integer(int64_t value) { return Value(value); }
  static Value Real(double value) { return Value(value); }
  static Value Text(std::string value) { return Value(std::move(value)); }
  static Value BlobValue(Blob value) { return Value(std::move(value)); }

  TypeId type() const;
  bool IsNull() const { return std::holds_alternative<std::monostate>(value_); }
  bool AsBoolean() const;
  int64_t AsInteger() const;
  double AsReal() const;
  const std::string& AsText() const;
  const Blob& AsBlob() const;
  std::string ToString() const;

  // SQL comparison. The optional is empty for UNKNOWN (a NULL comparison).
  std::optional<int> Compare(const Value& other) const;
  std::optional<bool> Equals(const Value& other) const;
  bool operator==(const Value& other) const;
  bool operator!=(const Value& other) const { return !(*this == other); }

  // Stable order-preserving representation used by indexes.
  std::string EncodeKey(bool descending = false) const;
  static std::optional<Value> DecodeKey(std::string_view encoded);

 private:
  std::variant<std::monostate, bool, int64_t, double, std::string, Blob> value_;
};

struct Column {
  std::string name;
  TypeId type = TypeId::kNull;
  bool nullable = true;
  bool primary_key = false;
  bool unique = false;
};

class Schema {
 public:
  Schema() = default;
  explicit Schema(std::vector<Column> columns) : columns_(std::move(columns)) {}
  const std::vector<Column>& columns() const { return columns_; }
  size_t size() const { return columns_.size(); }
  int Find(std::string_view name) const;
  const Column& operator[](size_t index) const { return columns_[index]; }

 private:
  std::vector<Column> columns_;
};

inline std::string EncodeKey(const Value& value, bool descending = false) {
  return value.EncodeKey(descending);
}
inline std::optional<Value> DecodeKey(std::string_view encoded) {
  return Value::DecodeKey(encoded);
}

}  // namespace tuplestone

#endif  // TUPLESTONE_VALUE_H_
