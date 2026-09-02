#ifndef TUPLESTONE_DISK_DISK_MANAGER_H_
#define TUPLESTONE_DISK_DISK_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"
#include "disk/page.h"
#include "tuplestone/status.h"

namespace tuplestone {

class DiskManager {
 public:
  DiskManager() = default;
  ~DiskManager();
  DiskManager(const DiskManager&) = delete;
  DiskManager& operator=(const DiskManager&) = delete;

  Status Open(std::string_view path, bool create_if_missing = true);
  Status Close();
  bool IsOpen() const { return file_.is_open(); }
  const std::string& path() const { return path_; }
  uint32_t page_count() const { return page_count_; }

  StatusOr<page_id_t> AllocatePage();
  Status ReadPage(page_id_t page_id, Page* page);
  Status WritePage(page_id_t page_id, const Page& page);
  Status FreePage(page_id_t page_id);
  Status Sync();

 protected:
  mutable std::mutex mutex_;
  std::fstream file_;
  std::string path_;
  uint32_t page_count_ = 0;
  page_id_t free_list_head_ = kInvalidPageId;
  uint64_t next_txn_id_ = 1;
  uint64_t checkpoint_lsn_ = 0;
  table_id_t next_table_id_ = 1;
  index_id_t next_index_id_ = 1;
  page_id_t catalog_roots_[3] = {kInvalidPageId, kInvalidPageId, kInvalidPageId};

  Status ReadHeaderLocked();
  Status WriteHeaderLocked();
  Status EnsurePageLocked(page_id_t page_id);
  Status WritePageLocked(page_id_t page_id, const Page& page);
};

// A deterministic fault-injecting wrapper used by crash/recovery tests.
class FaultDiskManager final : public DiskManager {
 public:
  void FailWriteAt(size_t write_number) { fail_write_at_ = write_number; }
  void TearWriteAt(size_t write_number) { tear_write_at_ = write_number; }
  void ResetFaults() {
    fail_write_at_ = 0;
    tear_write_at_ = 0;
    write_count_ = 0;
  }
  size_t write_count() const { return write_count_; }

  Status WritePage(page_id_t page_id, const Page& page);
  Status Sync();

 private:
  size_t write_count_ = 0;
  size_t fail_write_at_ = 0;
  size_t tear_write_at_ = 0;
};

}  // namespace tuplestone

#endif
