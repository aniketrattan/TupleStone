// A non-owning view of bytes. Distinct from std::string_view only in that it is
// explicitly about *bytes*, not characters, and compares with memcmp — which is
// exactly the operation the B+Tree performs on encoded keys (ARCHITECTURE.md §6.2).
#ifndef NANOSQL_COMMON_SLICE_H_
#define NANOSQL_COMMON_SLICE_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace nanosql {

class Slice {
 public:
  constexpr Slice() = default;
  constexpr Slice(const uint8_t* data, size_t size) : data_(data), size_(size) {}
  Slice(const char* data, size_t size)
      : data_(reinterpret_cast<const uint8_t*>(data)), size_(size) {}
  Slice(const std::string& s)  // NOLINT(google-explicit-constructor)
      : data_(reinterpret_cast<const uint8_t*>(s.data())), size_(s.size()) {}
  Slice(std::string_view s)  // NOLINT(google-explicit-constructor)
      : data_(reinterpret_cast<const uint8_t*>(s.data())), size_(s.size()) {}

  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  const uint8_t* begin() const { return data_; }
  const uint8_t* end() const { return data_ + size_; }

  // Precondition: i < size(). Bounds are a caller invariant, not a runtime check;
  // the hot paths that use this run inside already-validated page bodies.
  uint8_t operator[](size_t i) const { return data_[i]; }

  Slice Sub(size_t offset, size_t count) const { return Slice(data_ + offset, count); }
  void RemovePrefix(size_t n) {
    data_ += n;
    size_ -= n;
  }

  std::string ToString() const {
    return std::string(reinterpret_cast<const char*>(data_), size_);
  }
  std::string_view ToStringView() const {
    return std::string_view(reinterpret_cast<const char*>(data_), size_);
  }

  // Lexicographic over unsigned bytes, shorter-is-smaller on a common prefix.
  // Returns <0, 0, >0. The sign, not the magnitude, is the contract.
  int Compare(const Slice& other) const {
    const size_t n = size_ < other.size_ ? size_ : other.size_;
    // memcmp(_, _, 0) is defined, but the pointers may be null for an empty
    // Slice, and passing null to memcmp is UB even with n == 0.
    if (n != 0) {
      const int r = std::memcmp(data_, other.data_, n);
      if (r != 0) return r;
    }
    if (size_ == other.size_) return 0;
    return size_ < other.size_ ? -1 : 1;
  }

  bool StartsWith(const Slice& prefix) const {
    return size_ >= prefix.size_ && Sub(0, prefix.size_).Compare(prefix) == 0;
  }

  friend bool operator==(const Slice& a, const Slice& b) { return a.Compare(b) == 0; }
  friend bool operator!=(const Slice& a, const Slice& b) { return a.Compare(b) != 0; }
  friend bool operator<(const Slice& a, const Slice& b) { return a.Compare(b) < 0; }
  friend bool operator<=(const Slice& a, const Slice& b) { return a.Compare(b) <= 0; }
  friend bool operator>(const Slice& a, const Slice& b) { return a.Compare(b) > 0; }
  friend bool operator>=(const Slice& a, const Slice& b) { return a.Compare(b) >= 0; }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
};

}  // namespace nanosql

#endif  // NANOSQL_COMMON_SLICE_H_
