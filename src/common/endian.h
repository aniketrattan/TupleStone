// Explicit little-endian field access for every on-disk integer.
//
// ARCHITECTURE.md §3: never memcpy a struct to disk. These helpers are the only
// sanctioned way bytes become integers and back, which is what makes the file
// format independent of the host's endianness, struct padding, and alignment.
//
// Big-endian variants exist for exactly one reason: the memcomparable key
// encoding (ARCHITECTURE.md §6.2) needs most-significant-byte-first so that
// memcmp reproduces numeric order.
#ifndef NANOSQL_COMMON_ENDIAN_H_
#define NANOSQL_COMMON_ENDIAN_H_

#include <cstdint>
#include <cstring>

namespace nanosql {

inline void StoreU8(uint8_t* dst, uint8_t v) { dst[0] = v; }
inline uint8_t LoadU8(const uint8_t* src) { return src[0]; }

inline void StoreU16LE(uint8_t* dst, uint16_t v) {
  dst[0] = static_cast<uint8_t>(v);
  dst[1] = static_cast<uint8_t>(v >> 8);
}
inline uint16_t LoadU16LE(const uint8_t* src) {
  return static_cast<uint16_t>(static_cast<uint16_t>(src[0]) |
                               (static_cast<uint16_t>(src[1]) << 8));
}

inline void StoreU32LE(uint8_t* dst, uint32_t v) {
  dst[0] = static_cast<uint8_t>(v);
  dst[1] = static_cast<uint8_t>(v >> 8);
  dst[2] = static_cast<uint8_t>(v >> 16);
  dst[3] = static_cast<uint8_t>(v >> 24);
}
inline uint32_t LoadU32LE(const uint8_t* src) {
  return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

inline void StoreU64LE(uint8_t* dst, uint64_t v) {
  StoreU32LE(dst, static_cast<uint32_t>(v));
  StoreU32LE(dst + 4, static_cast<uint32_t>(v >> 32));
}
inline uint64_t LoadU64LE(const uint8_t* src) {
  return static_cast<uint64_t>(LoadU32LE(src)) |
         (static_cast<uint64_t>(LoadU32LE(src + 4)) << 32);
}

// Signed stores go through the unsigned path. The conversion is well defined in
// both directions in C++20, where signed integers are mandated two's complement.
inline void StoreI64LE(uint8_t* dst, int64_t v) { StoreU64LE(dst, static_cast<uint64_t>(v)); }
inline int64_t LoadI64LE(const uint8_t* src) { return static_cast<int64_t>(LoadU64LE(src)); }
inline void StoreI32LE(uint8_t* dst, int32_t v) { StoreU32LE(dst, static_cast<uint32_t>(v)); }
inline int32_t LoadI32LE(const uint8_t* src) { return static_cast<int32_t>(LoadU32LE(src)); }

// Bit-pattern moves between double and uint64_t. std::bit_cast would do, but a
// memcpy is equally correct, equally optimized, and needs no extra include.
inline uint64_t DoubleToBits(double d) {
  uint64_t bits = 0;
  std::memcpy(&bits, &d, sizeof(bits));
  return bits;
}
inline double BitsToDouble(uint64_t bits) {
  double d = 0.0;
  std::memcpy(&d, &bits, sizeof(d));
  return d;
}

inline void StoreF64LE(uint8_t* dst, double v) { StoreU64LE(dst, DoubleToBits(v)); }
inline double LoadF64LE(const uint8_t* src) { return BitsToDouble(LoadU64LE(src)); }

// Big-endian: for memcomparable keys only. Never for the file format.
inline void StoreU16BE(uint8_t* dst, uint16_t v) {
  dst[0] = static_cast<uint8_t>(v >> 8);
  dst[1] = static_cast<uint8_t>(v);
}
inline uint16_t LoadU16BE(const uint8_t* src) {
  return static_cast<uint16_t>((static_cast<uint16_t>(src[0]) << 8) |
                               static_cast<uint16_t>(src[1]));
}

inline void StoreU32BE(uint8_t* dst, uint32_t v) {
  dst[0] = static_cast<uint8_t>(v >> 24);
  dst[1] = static_cast<uint8_t>(v >> 16);
  dst[2] = static_cast<uint8_t>(v >> 8);
  dst[3] = static_cast<uint8_t>(v);
}
inline uint32_t LoadU32BE(const uint8_t* src) {
  return (static_cast<uint32_t>(src[0]) << 24) | (static_cast<uint32_t>(src[1]) << 16) |
         (static_cast<uint32_t>(src[2]) << 8) | static_cast<uint32_t>(src[3]);
}

inline void StoreU64BE(uint8_t* dst, uint64_t v) {
  StoreU32BE(dst, static_cast<uint32_t>(v >> 32));
  StoreU32BE(dst + 4, static_cast<uint32_t>(v));
}
inline uint64_t LoadU64BE(const uint8_t* src) {
  return (static_cast<uint64_t>(LoadU32BE(src)) << 32) | static_cast<uint64_t>(LoadU32BE(src + 4));
}

}  // namespace nanosql

#endif  // NANOSQL_COMMON_ENDIAN_H_
