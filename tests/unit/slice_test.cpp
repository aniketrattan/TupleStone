#include "common/slice.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace nanosql {
namespace {

Slice FromLiteral(const char* s) { return Slice(std::string_view(s)); }

TEST(SliceTest, DefaultIsEmpty) {
  const Slice s;
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.size(), 0u);
  EXPECT_EQ(s.data(), nullptr);
}

TEST(SliceTest, ViewsWithoutCopying) {
  const std::string owner = "hello";
  const Slice s(owner);
  EXPECT_EQ(s.size(), 5u);
  EXPECT_EQ(reinterpret_cast<const void*>(s.data()),
            reinterpret_cast<const void*>(owner.data()));
  EXPECT_EQ(s.ToString(), "hello");
  EXPECT_EQ(s.ToStringView(), "hello");
}

TEST(SliceTest, HandlesEmbeddedNuls) {
  const std::string owner("a\0b", 3);
  const Slice s(owner);
  EXPECT_EQ(s.size(), 3u);
  EXPECT_EQ(s[1], 0u);
  EXPECT_EQ(s.ToString().size(), 3u);
}

// The B+Tree relies on this being unsigned-byte lexicographic order: keys with
// a high byte must sort after keys without one (ARCHITECTURE.md §6.2).
TEST(SliceTest, ComparesBytesAsUnsigned) {
  const std::string low("\x01", 1);
  const std::string high("\xFF", 1);
  EXPECT_LT(Slice(low), Slice(high));
  EXPECT_GT(Slice(high), Slice(low));
}

TEST(SliceTest, ShorterPrefixSortsFirst) {
  EXPECT_LT(FromLiteral("ab"), FromLiteral("abc"));
  EXPECT_GT(FromLiteral("abc"), FromLiteral("ab"));
  EXPECT_EQ(FromLiteral("abc"), FromLiteral("abc"));
}

TEST(SliceTest, CompareReturnsOnlyMeaningfulSign) {
  EXPECT_LT(FromLiteral("a").Compare(FromLiteral("b")), 0);
  EXPECT_GT(FromLiteral("b").Compare(FromLiteral("a")), 0);
  EXPECT_EQ(FromLiteral("a").Compare(FromLiteral("a")), 0);
}

// A default Slice has a null data pointer; memcmp with null is UB even for a
// zero length, so Compare must not reach it.
TEST(SliceTest, EmptySlicesCompareEqualWithoutTouchingNull) {
  const Slice a;
  const Slice b;
  EXPECT_EQ(a.Compare(b), 0);
  EXPECT_EQ(a, b);
  EXPECT_LT(a, FromLiteral("x"));
}

TEST(SliceTest, SubAndRemovePrefix) {
  Slice s = FromLiteral("abcdef");
  EXPECT_EQ(s.Sub(2, 3), FromLiteral("cde"));
  s.RemovePrefix(2);
  EXPECT_EQ(s, FromLiteral("cdef"));
  s.RemovePrefix(4);
  EXPECT_TRUE(s.empty());
}

TEST(SliceTest, StartsWith) {
  const Slice s = FromLiteral("abcdef");
  EXPECT_TRUE(s.StartsWith(FromLiteral("abc")));
  EXPECT_TRUE(s.StartsWith(FromLiteral("abcdef")));
  EXPECT_TRUE(s.StartsWith(Slice()));
  EXPECT_FALSE(s.StartsWith(FromLiteral("abd")));
  EXPECT_FALSE(s.StartsWith(FromLiteral("abcdefg")));
}

TEST(SliceTest, IterationYieldsBytes) {
  const std::string owner("\x00\x01\xFF", 3);
  const Slice s(owner);
  const std::vector<uint8_t> seen(s.begin(), s.end());
  EXPECT_EQ(seen, (std::vector<uint8_t>{0x00, 0x01, 0xFF}));
}

TEST(SliceTest, OrderingMatchesStdStringForAsciiWithoutHighBytes) {
  const std::vector<std::string> words = {"", "a", "ab", "abc", "b", "ba", "z"};
  for (const std::string& x : words) {
    for (const std::string& y : words) {
      const int slice_sign = (Slice(x).Compare(Slice(y)) > 0) - (Slice(x).Compare(Slice(y)) < 0);
      const int string_sign = (x > y) - (x < y);
      EXPECT_EQ(slice_sign, string_sign) << "\"" << x << "\" vs \"" << y << "\"";
    }
  }
}

}  // namespace
}  // namespace nanosql
