#include "common/crc32c.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace nanosql {
namespace {

constexpr uint64_t kSeed = 0x63726333326300ull;

Slice Bytes(const std::string& s) { return Slice(s); }

// The published CRC32C check value. If this fails, the polynomial or the
// reflection is wrong and every checksum in the file format is wrong with it.
TEST(Crc32cTest, MatchesTheStandardCheckVector) {
  EXPECT_EQ(Crc32c(Bytes("123456789")), 0xE3069283u);
}

TEST(Crc32cTest, KnownAnswersFromRfc3720AppendixB) {
  // 32 bytes of zeros, then 32 bytes of 0xFF, then 0x00..0x1F ascending.
  const std::vector<uint8_t> zeros(32, 0x00);
  EXPECT_EQ(Crc32c(zeros.data(), zeros.size()), 0x8A9136AAu);

  const std::vector<uint8_t> ones(32, 0xFF);
  EXPECT_EQ(Crc32c(ones.data(), ones.size()), 0x62A8AB43u);

  std::vector<uint8_t> ascending(32);
  for (size_t i = 0; i < ascending.size(); ++i) ascending[i] = static_cast<uint8_t>(i);
  EXPECT_EQ(Crc32c(ascending.data(), ascending.size()), 0x46DD794Eu);
}

TEST(Crc32cTest, EmptyInputIsZero) {
  EXPECT_EQ(Crc32c(nullptr, 0), 0u);
  EXPECT_EQ(Crc32c(Slice()), 0u);
}

// The WAL appends records in pieces, so a split feed must equal a whole feed.
TEST(Crc32cTest, ExtendComposesAcrossEverySplitPoint) {
  const std::string data = "the quick brown fox jumps over the lazy dog, 0123456789";
  const uint32_t whole = Crc32c(Bytes(data));
  const auto* p = reinterpret_cast<const uint8_t*>(data.data());
  for (size_t split = 0; split <= data.size(); ++split) {
    const uint32_t first = Crc32c(p, split);
    const uint32_t both = Crc32cExtend(first, p + split, data.size() - split);
    EXPECT_EQ(both, whole) << "split at " << split;
  }
}

TEST(Crc32cTest, DetectsEverySingleBitFlipInAPage) {
  std::mt19937_64 rng(kSeed);
  std::vector<uint8_t> page(4096);
  for (uint8_t& byte : page) byte = static_cast<uint8_t>(rng());
  const uint32_t original = Crc32c(page.data(), page.size());

  // Exhaustive over one byte at each of several positions rather than all 32768
  // bits, to keep the test fast while still covering the whole page's span.
  for (size_t offset = 0; offset < page.size(); offset += 37) {
    for (int bit = 0; bit < 8; ++bit) {
      page[offset] ^= static_cast<uint8_t>(1u << bit);
      EXPECT_NE(Crc32c(page.data(), page.size()), original)
          << "seed=" << kSeed << " offset=" << offset << " bit=" << bit;
      page[offset] ^= static_cast<uint8_t>(1u << bit);
    }
  }
  EXPECT_EQ(Crc32c(page.data(), page.size()), original) << "restore failed";
}

// A torn write typically replaces a tail with stale or zero bytes. The checksum
// has to notice, since that is the exact failure M1's torn-write test injects.
TEST(Crc32cTest, DetectsTruncatedAndZeroedTails) {
  std::mt19937_64 rng(kSeed + 1);
  std::vector<uint8_t> page(4096);
  for (uint8_t& byte : page) byte = static_cast<uint8_t>(rng() | 1u);  // avoid incidental zeros
  const uint32_t original = Crc32c(page.data(), page.size());

  for (const size_t kept : {size_t{512}, size_t{1024}, size_t{2048}, size_t{4095}}) {
    std::vector<uint8_t> torn = page;
    std::memset(torn.data() + kept, 0, torn.size() - kept);
    EXPECT_NE(Crc32c(torn.data(), torn.size()), original) << "kept=" << kept;
  }
}

TEST(Crc32cTest, IsStableAcrossRepeatedCalls) {
  const std::string data = "stability";
  EXPECT_EQ(Crc32c(Bytes(data)), Crc32c(Bytes(data)));
}

TEST(Crc32cTest, DiffersForDifferentLengthsOfZeros) {
  // Length must be part of the checksum's input, or a truncation to a zero-fill
  // boundary would go undetected.
  const std::vector<uint8_t> a(16, 0);
  const std::vector<uint8_t> b(17, 0);
  EXPECT_NE(Crc32c(a.data(), a.size()), Crc32c(b.data(), b.size()));
}

}  // namespace
}  // namespace nanosql
