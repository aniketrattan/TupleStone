#ifndef TUPLESTONE_BUFFER_BUFFER_POOL_MANAGER_H_
#define TUPLESTONE_BUFFER_BUFFER_POOL_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "disk/disk_manager.h"
#include "tuplestone/status.h"

namespace tuplestone {
class BufferPoolManager;
class PageGuard {
 public:
  PageGuard() = default;
  PageGuard(BufferPoolManager* manager, page_id_t page_id, Page* page, bool writable)
      : manager_(manager), page_id_(page_id), page_(page), writable_(writable) {}
  PageGuard(const PageGuard&) = delete;
  PageGuard& operator=(const PageGuard&) = delete;
  PageGuard(PageGuard&& other) noexcept { *this = std::move(other); }
  PageGuard& operator=(PageGuard&& other) noexcept;
  ~PageGuard();
  bool valid() const { return page_ != nullptr; }
  Page* operator->() { return page_; }
  const Page* operator->() const { return page_; }
  Page& operator*() { return *page_; }
  const Page& operator*() const { return *page_; }
  void Drop();

 private:
  BufferPoolManager* manager_ = nullptr;
  page_id_t page_id_ = kInvalidPageId;
  Page* page_ = nullptr;
  bool writable_ = false;
};

class BufferPoolManager {
 public:
  BufferPoolManager(size_t pool_size, DiskManager* disk_manager);
  BufferPoolManager(const BufferPoolManager&) = delete;
  BufferPoolManager& operator=(const BufferPoolManager&) = delete;
  ~BufferPoolManager();
  StatusOr<PageGuard> FetchPage(page_id_t page_id, bool writable = false);
  StatusOr<PageGuard> NewPage(page_id_t* page_id);
  Status UnpinPage(page_id_t page_id, bool dirty);
  Status FlushPage(page_id_t page_id);
  Status FlushAllPages();
  Status DeletePage(page_id_t page_id);
  size_t Size() const { return frames_.size(); }
  size_t PinnedPages() const;

 private:
  struct Frame {
    Page page;
    size_t pin_count = 0;
    bool dirty = false;
    uint64_t age = 0;
  };
  friend class PageGuard;
  void Release(page_id_t page_id, bool dirty);
  std::optional<size_t> VictimLocked();
  size_t pool_size_;
  DiskManager* disk_manager_;
  mutable std::mutex mutex_;
  std::vector<Frame> frames_;
  std::unordered_map<page_id_t, size_t> page_table_;
  uint64_t clock_ = 0;
};
}  // namespace tuplestone
#endif
