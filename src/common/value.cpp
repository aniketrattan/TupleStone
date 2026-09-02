#include "tuplestone/value.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace tuplestone {

Value::Value(const Value& other) : value_(std::monostate{}) {
  switch (other.type()) {
    case TypeId::kNull:
      break;
    case TypeId::kBoolean:
      value_.emplace<bool>(other.AsBoolean());
      break;
    case TypeId::kInteger:
      value_.emplace<int64_t>(other.AsInteger());
      break;
    case TypeId::kReal:
      value_.emplace<double>(other.AsReal());
      break;
    case TypeId::kText:
      value_.emplace<std::string>(other.AsText());
      break;
    case TypeId::kBlob:
      value_.emplace<Blob>(other.AsBlob());
      break;
  }
}

Value& Value::operator=(const Value& other) {
  if (this == &other) return *this;
  switch (other.type()) {
    case TypeId::kNull:
      value_.emplace<std::monostate>();
      break;
    case TypeId::kBoolean:
      value_.emplace<bool>(other.AsBoolean());
      break;
    case TypeId::kInteger:
      value_.emplace<int64_t>(other.AsInteger());
      break;
    case TypeId::kReal:
      value_.emplace<double>(other.AsReal());
      break;
    case TypeId::kText:
      value_.emplace<std::string>(other.AsText());
      break;
    case TypeId::kBlob:
      value_.emplace<Blob>(other.AsBlob());
      break;
  }
  return *this;
}

const char* TypeIdName(TypeId type) {
  switch (type) {
    case TypeId::kNull:
      return "NULL";
    case TypeId::kBoolean:
      return "BOOLEAN";
    case TypeId::kInteger:
      return "INTEGER";
    case TypeId::kReal:
      return "REAL";
    case TypeId::kText:
      return "TEXT";
    case TypeId::kBlob:
      return "BLOB";
  }
  return "UNKNOWN";
}

TypeId Value::type() const {
  switch (value_.index()) {
    case 0:
      return TypeId::kNull;
    case 1:
      return TypeId::kBoolean;
    case 2:
      return TypeId::kInteger;
    case 3:
      return TypeId::kReal;
    case 4:
      return TypeId::kText;
    case 5:
      return TypeId::kBlob;
    default:
      return TypeId::kNull;
  }
}

bool Value::AsBoolean() const {
  return std::get<bool>(value_);
}
int64_t Value::AsInteger() const {
  return std::get<int64_t>(value_);
}
double Value::AsReal() const {
  return std::get<double>(value_);
}
const std::string& Value::AsText() const {
  return std::get<std::string>(value_);
}
const Value::Blob& Value::AsBlob() const {
  return std::get<Blob>(value_);
}

std::string Value::ToString() const {
  switch (type()) {
    case TypeId::kNull:
      return "NULL";
    case TypeId::kBoolean:
      return AsBoolean() ? "TRUE" : "FALSE";
    case TypeId::kInteger:
      return std::to_string(AsInteger());
    case TypeId::kReal: {
      std::ostringstream out;
      out << std::setprecision(std::numeric_limits<double>::max_digits10) << AsReal();
      return out.str();
    }
    case TypeId::kText:
      return AsText();
    case TypeId::kBlob: {
      std::ostringstream out;
      out << "X'" << std::hex << std::setfill('0');
      for (const uint8_t byte : AsBlob()) out << std::setw(2) << static_cast<unsigned>(byte);
      out << "'";
      return out.str();
    }
  }
  return {};
}

std::optional<int> Value::Compare(const Value& other) const {
  if (IsNull() || other.IsNull()) return std::nullopt;
  if ((type() == TypeId::kInteger || type() == TypeId::kReal) &&
      (other.type() == TypeId::kInteger || other.type() == TypeId::kReal)) {
    const double left = type() == TypeId::kInteger ? static_cast<double>(AsInteger()) : AsReal();
    const double right =
        other.type() == TypeId::kInteger ? static_cast<double>(other.AsInteger()) : other.AsReal();
    if (std::isnan(left) || std::isnan(right)) return std::nullopt;
    return (left > right) - (left < right);
  }
  if (type() != other.type()) return std::nullopt;
  switch (type()) {
    case TypeId::kBoolean:
      return (AsBoolean() > other.AsBoolean()) - (AsBoolean() < other.AsBoolean());
    case TypeId::kText:
      return (AsText() > other.AsText()) - (AsText() < other.AsText());
    case TypeId::kBlob:
      return (AsBlob() > other.AsBlob()) - (AsBlob() < other.AsBlob());
    case TypeId::kInteger:
      return (AsInteger() > other.AsInteger()) - (AsInteger() < other.AsInteger());
    case TypeId::kReal:
      return (AsReal() > other.AsReal()) - (AsReal() < other.AsReal());
    case TypeId::kNull:
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<bool> Value::Equals(const Value& other) const {
  const std::optional<int> compared = Compare(other);
  if (!compared.has_value()) return std::nullopt;
  return *compared == 0;
}

bool Value::operator==(const Value& other) const {
  if (IsNull() || other.IsNull()) return IsNull() && other.IsNull();
  const std::optional<bool> equal = Equals(other);
  return equal.value_or(false);
}

namespace {

void AppendBigEndian(std::string* out, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8)
    out->push_back(static_cast<char>((value >> shift) & 0xFFu));
}

uint64_t ReadBigEndian(std::string_view bytes) {
  uint64_t result = 0;
  for (const char byte : bytes) {
    result = (result << 8) | static_cast<uint8_t>(static_cast<unsigned char>(byte));
  }
  return result;
}

void AppendEscaped(std::string* out, std::string_view text) {
  for (size_t offset = 0; offset < text.size(); offset += 8) {
    const size_t count = std::min<size_t>(8, text.size() - offset);
    out->append(text.substr(offset, count));
    out->append(8 - count, '\0');
    out->push_back(static_cast<char>(count == 8 && offset + count < text.size() ? 0xFF : count));
  }
  if (text.empty()) out->push_back('\0');
}

}  // namespace

std::string Value::EncodeKey(bool descending) const {
  std::string result;
  result.reserve(16);
  result.push_back(static_cast<char>(IsNull() ? 0 : 1));
  if (!IsNull()) {
    result.push_back(static_cast<char>(type()));
    switch (type()) {
      case TypeId::kBoolean:
        result.push_back(static_cast<char>(AsBoolean() ? 1 : 0));
        break;
      case TypeId::kInteger:
        AppendBigEndian(&result, static_cast<uint64_t>(AsInteger()) ^ 0x8000000000000000ull);
        break;
      case TypeId::kReal: {
        uint64_t bits = std::bit_cast<uint64_t>(AsReal());
        bits = (bits & 0x8000000000000000ull) != 0 ? ~bits : bits ^ 0x8000000000000000ull;
        AppendBigEndian(&result, bits);
        break;
      }
      case TypeId::kText:
        AppendEscaped(&result, AsText());
        break;
      case TypeId::kBlob:
        AppendEscaped(&result, std::string_view(reinterpret_cast<const char*>(AsBlob().data()),
                                                AsBlob().size()));
        break;
      case TypeId::kNull:
        break;
    }
  }
  if (descending) {
    for (char& byte : result) byte = static_cast<char>(~static_cast<unsigned char>(byte));
  }
  return result;
}

std::optional<Value> Value::DecodeKey(std::string_view encoded) {
  if (encoded.empty()) return std::nullopt;
  const uint8_t marker = static_cast<uint8_t>(encoded.front());
  if (marker == 0) return Value();
  if (marker != 1 || encoded.size() < 3) return std::nullopt;
  const TypeId type = static_cast<TypeId>(static_cast<uint8_t>(encoded[1]));
  const std::string_view payload = encoded.substr(2);
  switch (type) {
    case TypeId::kBoolean:
      return payload.size() == 1 && (payload[0] == 0 || payload[0] == 1)
                 ? std::optional<Value>(Value(payload[0] != 0))
                 : std::nullopt;
    case TypeId::kInteger:
      if (payload.size() != 8) return std::nullopt;
      return Value(static_cast<int64_t>(ReadBigEndian(payload) ^ 0x8000000000000000ull));
    case TypeId::kReal: {
      if (payload.size() != 8) return std::nullopt;
      uint64_t bits = ReadBigEndian(payload);
      bits = (bits & 0x8000000000000000ull) != 0 ? bits ^ 0x8000000000000000ull : ~bits;
      return Value(std::bit_cast<double>(bits));
    }
    case TypeId::kText:
    case TypeId::kBlob: {
      std::string bytes;
      size_t pos = 0;
      while (pos + 9 <= payload.size()) {
        const uint8_t count = static_cast<uint8_t>(payload[pos + 8]);
        if (count > 8) {
          if (count != 0xFF) return std::nullopt;
          bytes.append(payload.substr(pos, 8));
          pos += 9;
        } else {
          bytes.append(payload.substr(pos, count));
          pos += 9;
          if (pos != payload.size()) return std::nullopt;
          if (type == TypeId::kText) return Value(std::move(bytes));
          Blob blob(bytes.begin(), bytes.end());
          return Value(std::move(blob));
        }
      }
      return std::nullopt;
    }
    case TypeId::kNull:
      return std::nullopt;
  }
  return std::nullopt;
}

int Schema::Find(std::string_view name) const {
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].name.size() != name.size()) continue;
    bool equal = true;
    for (size_t j = 0; j < name.size(); ++j) {
      const auto left = static_cast<unsigned char>(columns_[i].name[j]);
      const auto right = static_cast<unsigned char>(name[j]);
      if (std::tolower(left) != std::tolower(right)) {
        equal = false;
        break;
      }
    }
    if (equal) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace tuplestone
