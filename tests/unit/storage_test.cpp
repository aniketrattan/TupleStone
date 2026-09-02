#include "disk/disk_manager.h"
#include "index/b_plus_tree.h"
#include "wal/log_manager.h"

#include <cstdio>
#include <fstream>

#include <gtest/gtest.h>

namespace tuplestone {
namespace {

TEST(StorageTest, DiskRoundTripChecksumAndFreeList) {
  const char* path = "storage_test.db";
  std::remove(path);
  DiskManager disk;
  ASSERT_TRUE(disk.Open(path).ok());
  const auto page_id = disk.AllocatePage();
  ASSERT_TRUE(page_id.ok());
  Page page(*page_id);
  page.data()[32] = 0xA5;
  ASSERT_TRUE(disk.WritePage(*page_id, page).ok());
  ASSERT_TRUE(disk.Close().ok());
  ASSERT_TRUE(disk.Open(path, false).ok());
  Page loaded;
  ASSERT_TRUE(disk.ReadPage(*page_id, &loaded).ok());
  EXPECT_EQ(loaded.data()[32], 0xA5);
  ASSERT_TRUE(disk.FreePage(*page_id).ok());
  const auto reused = disk.AllocatePage();
  ASSERT_TRUE(reused.ok());
  EXPECT_EQ(*reused, *page_id);
  disk.Close();
  std::remove(path);
}

TEST(StorageTest, OrderedIndexSupportsDuplicatesAndRanges) {
  BPlusTree index(false);
  ASSERT_TRUE(index.Insert("b", RID{1, 1}).ok());
  ASSERT_TRUE(index.Insert("a", RID{1, 0}).ok());
  ASSERT_TRUE(index.Insert("b", RID{2, 1}).ok());
  EXPECT_EQ(index.SearchAll("b").size(), 2u);
  EXPECT_EQ(index.Range("a").size(), 3u);
  EXPECT_TRUE(index.Validate().ok());
  ASSERT_TRUE(index.Remove("b", RID{1, 1}).ok());
  EXPECT_EQ(index.SearchAll("b").size(), 1u);
}

TEST(StorageTest, WalRoundTripsAndDetectsRecords) {
  const char* path = "storage_test.wal";
  std::remove(path);
  LogManager log;
  ASSERT_TRUE(log.Open(path).ok());
  const auto lsn = log.Append(7, 0, LogRecordType::kBegin, "payload");
  ASSERT_TRUE(lsn.ok());
  ASSERT_TRUE(log.Flush(*lsn).ok());
  {
    std::ifstream inspect(path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(inspect.good());
    EXPECT_GT(inspect.tellg(), static_cast<std::streamoff>(0));
  }
  const auto records = log.ReadAll();
  ASSERT_TRUE(records.ok());
  ASSERT_EQ(records->size(), 1u);
  EXPECT_EQ(records->front().txn_id, 7u);
  EXPECT_EQ(records->front().payload, "payload");
  log.Close();
  {
    std::ofstream torn(path, std::ios::binary | std::ios::app);
    torn.write("\x04\x00", 2);
  }
  LogManager recovered;
  ASSERT_TRUE(recovered.Open(path).ok());
  const auto valid_prefix = recovered.ReadAll();
  ASSERT_TRUE(valid_prefix.ok());
  ASSERT_EQ(valid_prefix->size(), 1u);
  EXPECT_EQ(valid_prefix->front().lsn, *lsn);
  const auto appended = recovered.Append(8, *lsn, LogRecordType::kCommit, "after-tail");
  ASSERT_TRUE(appended.ok());
  ASSERT_TRUE(recovered.Flush(*appended).ok());
  const auto repaired = recovered.ReadAll();
  ASSERT_TRUE(repaired.ok());
  ASSERT_EQ(repaired->size(), 2u);
  EXPECT_EQ(repaired->back().payload, "after-tail");
  recovered.Close();
  std::remove(path);
}

}  // namespace
}  // namespace tuplestone
