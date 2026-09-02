#include "common/crc32c.h"

#include <array>

namespace tuplestone {
namespace {

// Reflected Castagnoli polynomial. The bit-reversed form lets the update step
// shift right, which is what makes the table lookup a single xor-and-shift.
constexpr uint32_t kReflectedPoly = 0x82F63B78u;

using Table = std::array<uint32_t, 256>;

constexpr Table MakeTable() {
  Table table{};
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t crc = i;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) != 0 ? (crc >> 1) ^ kReflectedPoly : (crc >> 1);
    }
    table[i] = crc;
  }
  return table;
}

// Built at compile time, so there is no initialization order or thread-safety
// question about when the table becomes valid.
constexpr Table kTable = MakeTable();

}  // namespace

uint32_t Crc32cExtend(uint32_t prior_crc, const uint8_t* data, size_t size) {
  uint32_t crc = ~prior_crc;  // undo the caller's post-invert to resume the raw register
  for (size_t i = 0; i < size; ++i) {
    crc = kTable[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  }
  return ~crc;
}

uint32_t Crc32c(const uint8_t* data, size_t size) {
  // Crc32cExtend(0, ...) resumes from ~0, which is the standard initial register.
  return Crc32cExtend(0, data, size);
}

}  // namespace tuplestone
