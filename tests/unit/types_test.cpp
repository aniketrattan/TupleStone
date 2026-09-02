#include "common/types.h"

#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "common/endian.h"
#include "tuplestone/value.h"

namespace tuplestone {
namespace {

TEST(TypesTest, PageSizeConstantsAgree) {
  EXPECT_EQ(kPageSize, 4096u);
  EXPECT_EQ(size_t{1} << kPageSizeLog2, kPageSize);
}

TEST(TypesTest, SentinelsAreOutOfTheValidRange) {
  EXPECT_EQ(kInvalidPageId, 0xFFFFFFFFu);
  EXPECT_EQ(kInvalidLsn, 0u);
  EXPECT_EQ(kInvalidTxnId, 0u);
  // Page 0 is the file header, so it is never handed out as a data page.
  EXPECT_EQ(kHeaderPageId, 0u);
  EXPECT_NE(kHeaderPageId, kInvalidPageId);
}

TEST(RidTest, DefaultIsInvalid) {
  const RID rid;
  EXPECT_FALSE(rid.valid());
  EXPECT_EQ(rid.page_id, kInvalidPageId);
}

TEST(RidTest, EqualityAndOrdering) {
  const RID a{1, 0};
  const RID b{1, 1};
  const RID c{2, 0};
  EXPECT_EQ(a, (RID{1, 0}));
  EXPECT_NE(a, b);
  EXPECT_LT(a, b);
  EXPECT_LT(b, c);
  EXPECT_FALSE(c < a);
}

TEST(RidTest, OrderingIsAStrictWeakOrdering) {
  std::set<RID> unique;
  for (page_id_t p = 0; p < 8; ++p) {
    for (slot_id_t s = 0; s < 8; ++s) {
      unique.insert(RID{p, s});
    }
  }
  EXPECT_EQ(unique.size(), 64u);
  EXPECT_EQ(*unique.begin(), (RID{0, 0}));
  EXPECT_EQ(*unique.rbegin(), (RID{7, 7}));
}

// ARCHITECTURE.md §4.4 gives the RID six bytes inside a tuple header. Nothing
// serializes one yet, but the size the layout budgets for must be the size the
// fields need.
TEST(RidTest, SerializedSizeMatchesTheTupleHeaderBudget) {
  EXPECT_EQ(kRidSerializedSize, sizeof(page_id_t) + sizeof(slot_id_t));
  EXPECT_EQ(kRidSerializedSize, 6u);

  uint8_t buf[kRidSerializedSize];
  const RID rid{0x01020304u, 0x0506};
  StoreU32LE(buf, rid.page_id);
  StoreU16LE(buf + 4, rid.slot_id);
  EXPECT_EQ(LoadU32LE(buf), rid.page_id);
  EXPECT_EQ(LoadU16LE(buf + 4), rid.slot_id);
}

// The catalog stores TypeId numerically (ARCHITECTURE.md §10), so these
// discriminants are on-disk format and may not be renumbered.
TEST(TypeIdTest, DiscriminantsAreFrozen) {
  EXPECT_EQ(static_cast<int>(TypeId::kNull), 0);
  EXPECT_EQ(static_cast<int>(TypeId::kBoolean), 1);
  EXPECT_EQ(static_cast<int>(TypeId::kInteger), 2);
  EXPECT_EQ(static_cast<int>(TypeId::kReal), 3);
  EXPECT_EQ(static_cast<int>(TypeId::kText), 4);
  EXPECT_EQ(static_cast<int>(TypeId::kBlob), 5);
}

TEST(TypeIdTest, NamesMatchTheSqlSpelling) {
  EXPECT_STREQ(TypeIdName(TypeId::kNull), "NULL");
  EXPECT_STREQ(TypeIdName(TypeId::kBoolean), "BOOLEAN");
  EXPECT_STREQ(TypeIdName(TypeId::kInteger), "INTEGER");
  EXPECT_STREQ(TypeIdName(TypeId::kReal), "REAL");
  EXPECT_STREQ(TypeIdName(TypeId::kText), "TEXT");
  EXPECT_STREQ(TypeIdName(TypeId::kBlob), "BLOB");
}

}  // namespace
}  // namespace tuplestone
