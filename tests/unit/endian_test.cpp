#include "common/endian.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include <gtest/gtest.h>

namespace tuplestone {
namespace {

// Deterministic by construction. ARCHITECTURE-adjacent rule from CONTRIBUTING.md:
// every RNG is explicitly seeded and the seed is printed on failure.
constexpr uint64_t kSeed = 0x5455504C45535431ull;  // "TUPLEST1"

// The point of these helpers is that the byte order is fixed by the format, not
// by the host. Asserting the exact bytes is what makes that testable.
TEST(EndianTest, LittleEndianByteOrderIsExplicit) {
  uint8_t buf[8] = {};

  StoreU16LE(buf, 0x1234);
  EXPECT_EQ(buf[0], 0x34);
  EXPECT_EQ(buf[1], 0x12);

  StoreU32LE(buf, 0x12345678u);
  EXPECT_EQ(buf[0], 0x78);
  EXPECT_EQ(buf[1], 0x56);
  EXPECT_EQ(buf[2], 0x34);
  EXPECT_EQ(buf[3], 0x12);

  StoreU64LE(buf, 0x0123456789ABCDEFull);
  EXPECT_EQ(buf[0], 0xEF);
  EXPECT_EQ(buf[7], 0x01);
}

TEST(EndianTest, BigEndianByteOrderIsExplicit) {
  uint8_t buf[8] = {};

  StoreU16BE(buf, 0x1234);
  EXPECT_EQ(buf[0], 0x12);
  EXPECT_EQ(buf[1], 0x34);

  StoreU32BE(buf, 0x12345678u);
  EXPECT_EQ(buf[0], 0x12);
  EXPECT_EQ(buf[3], 0x78);

  StoreU64BE(buf, 0x0123456789ABCDEFull);
  EXPECT_EQ(buf[0], 0x01);
  EXPECT_EQ(buf[7], 0xEF);
}

TEST(EndianTest, RoundTripsBoundaryValues) {
  uint8_t buf[8] = {};

  for (const uint16_t v : {uint16_t{0}, uint16_t{1}, uint16_t{0x7FFF}, uint16_t{0xFFFF}}) {
    StoreU16LE(buf, v);
    EXPECT_EQ(LoadU16LE(buf), v);
    StoreU16BE(buf, v);
    EXPECT_EQ(LoadU16BE(buf), v);
  }
  for (const uint32_t v : {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu}) {
    StoreU32LE(buf, v);
    EXPECT_EQ(LoadU32LE(buf), v);
    StoreU32BE(buf, v);
    EXPECT_EQ(LoadU32BE(buf), v);
  }
  for (const uint64_t v :
       {0ull, 1ull, 0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull}) {
    StoreU64LE(buf, v);
    EXPECT_EQ(LoadU64LE(buf), v);
    StoreU64BE(buf, v);
    EXPECT_EQ(LoadU64BE(buf), v);
  }
}

TEST(EndianTest, SignedRoundTripsIncludingNegatives) {
  uint8_t buf[8] = {};
  for (const int64_t v : {std::numeric_limits<int64_t>::min(), int64_t{-1}, int64_t{0}, int64_t{1},
                          std::numeric_limits<int64_t>::max()}) {
    StoreI64LE(buf, v);
    EXPECT_EQ(LoadI64LE(buf), v);
  }
  for (const int32_t v :
       {std::numeric_limits<int32_t>::min(), -1, 0, 1, std::numeric_limits<int32_t>::max()}) {
    StoreI32LE(buf, v);
    EXPECT_EQ(LoadI32LE(buf), v);
  }
}

TEST(EndianTest, DoubleRoundTripsIncludingSpecialValues) {
  uint8_t buf[8] = {};
  const std::vector<double> values = {0.0,
                                      -0.0,
                                      1.0,
                                      -1.0,
                                      3.14159265358979,
                                      std::numeric_limits<double>::min(),
                                      std::numeric_limits<double>::max(),
                                      std::numeric_limits<double>::denorm_min(),
                                      std::numeric_limits<double>::infinity(),
                                      -std::numeric_limits<double>::infinity()};
  for (const double v : values) {
    StoreF64LE(buf, v);
    EXPECT_EQ(LoadF64LE(buf), v) << v;
  }

  // NaN never compares equal to itself, so the bit pattern is the assertion.
  const double nan = std::numeric_limits<double>::quiet_NaN();
  StoreF64LE(buf, nan);
  EXPECT_TRUE(std::isnan(LoadF64LE(buf)));

  // -0.0 and +0.0 compare equal but must not share an encoding here; that
  // collapsing happens only in the M4 key encoding, deliberately.
  StoreF64LE(buf, -0.0);
  EXPECT_EQ(LoadU64LE(buf), 0x8000000000000000ull);
  StoreF64LE(buf, 0.0);
  EXPECT_EQ(LoadU64LE(buf), 0ull);
}

TEST(EndianTest, RandomRoundTripAtUnalignedOffsets) {
  std::mt19937_64 rng(kSeed);
  std::vector<uint8_t> buf(64, 0);
  for (int i = 0; i < 10000; ++i) {
    const uint64_t v = rng();
    // Offsets 0..7 exercise every misalignment; the helpers must not care.
    const size_t offset = static_cast<size_t>(rng() % 8);
    StoreU64LE(buf.data() + offset, v);
    EXPECT_EQ(LoadU64LE(buf.data() + offset), v) << "seed=" << kSeed << " iter=" << i;
    StoreU64BE(buf.data() + offset, v);
    EXPECT_EQ(LoadU64BE(buf.data() + offset), v) << "seed=" << kSeed << " iter=" << i;
  }
}

// Big-endian is in the codebase for exactly one reason: it makes memcmp on the
// stored bytes agree with numeric order. That is the property M4 depends on.
TEST(EndianTest, BigEndianStorePreservesUnsignedOrderUnderMemcmp) {
  std::mt19937_64 rng(kSeed);
  for (int i = 0; i < 20000; ++i) {
    const uint64_t a = rng();
    const uint64_t b = rng();
    uint8_t ab[8];
    uint8_t bb[8];
    StoreU64BE(ab, a);
    StoreU64BE(bb, b);
    const int mem = std::memcmp(ab, bb, 8);
    const int num = (a > b) - (a < b);
    EXPECT_EQ((mem > 0) - (mem < 0), num) << "seed=" << kSeed << " a=" << a << " b=" << b;
  }
}

TEST(EndianTest, DoubleBitsRoundTrip) {
  EXPECT_EQ(BitsToDouble(DoubleToBits(1.5)), 1.5);
  EXPECT_EQ(DoubleToBits(1.0), 0x3FF0000000000000ull);
}

}  // namespace
}  // namespace tuplestone
