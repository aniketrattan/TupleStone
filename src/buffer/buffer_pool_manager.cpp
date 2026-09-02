#include "buffer/buffer_pool_manager.h"

#include <limits>

namespace tuplestone {
PageGuard& PageGuard::operator=(PageGuard&& other) noexcept {
  if (this != &other) {
    Drop();
    manager_ = other.manager_;
    page_id_ = other.page_id_;
    page_ = other.page_;
    writable_ = other.writable_;
    other.manager_ = nullptr;
    other.page_ = nullptr;
    other.page_id_ = kInvalidPageId;
  }
  return *this;
}
PageGuard::~PageGuard() {
  Drop();
}
void PageGuard::Drop() {
  if (manager_ != nullptr && page_ != nullptr) manager_->Release(page_id_, writable_);
  manager_ = nullptr;
  page_ = nullptr;
  page_id_ = kInvalidPageId;
}
BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager* disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
  frames_.resize(pool_size_);
}
BufferPoolManager::~BufferPoolManager() {
  (void)FlushAllPages();
}
std::optional<size_t> BufferPoolManager::VictimLocked() {
  size_t victim = frames_.size();
  uint64_t oldest = std::numeric_limits<uint64_t>::max();
  for (size_t i = 0; i < frames_.size(); ++i)
    if (frames_[i].pin_count == 0 && frames_[i].page.id() != kInvalidPageId &&
        frames_[i].age < oldest) {
      oldest = frames_[i].age;
      victim = i;
    }
  if (victim == frames_.size())
    for (size_t i = 0; i < frames_.size(); ++i)
      if (frames_[i].pin_count == 0) {
        victim = i;
        break;
      }
  if (victim == frames_.size()) return std::nullopt;
  return victim;
}
StatusOr<PageGuard> BufferPoolManager::FetchPage(page_id_t page_id, bool writable) {
  if (disk_manager_ == nullptr) return InvalidArgument("buffer pool has no disk manager");
  std::lock_guard lock(mutex_);
  auto existing = page_table_.find(page_id);
  if (existing != page_table_.end()) {
    Frame& frame = frames_[existing->second];
    ++frame.pin_count;
    frame.age = ++clock_;
    return PageGuard(this, page_id, &frame.page, writable);
  }
  auto victim = VictimLocked();
  if (!victim.has_value()) return OutOfMemory("all buffer pages are pinned");
  Frame& frame = frames_[*victim];
  if (frame.page.id() != kInvalidPageId) {
    if (frame.dirty) {
      const Status status = disk_manager_->WritePage(frame.page.id(), frame.page);
      if (!status.ok()) return status;
    }
    page_table_.erase(frame.page.id());
  }
  frame.page.Reset();
  TUPLESTONE_RETURN_IF_ERROR(disk_manager_->ReadPage(page_id, &frame.page));
  frame.page.set_id(page_id);
  frame.pin_count = 1;
  frame.dirty = false;
  frame.age = ++clock_;
  page_table_[page_id] = *victim;
  return PageGuard(this, page_id, &frame.page, writable);
}
StatusOr<PageGuard> BufferPoolManager::NewPage(page_id_t* page_id) {
  if (page_id == nullptr || disk_manager_ == nullptr)
    return InvalidArgument("invalid new-page request");
  auto allocated = disk_manager_->AllocatePage();
  if (!allocated.ok()) return allocated.status();
  auto guard = FetchPage(*allocated, true);
  if (!guard.ok()) {
    (void)disk_manager_->FreePage(*allocated);
    return guard.status();
  }
  *page_id = *allocated;
  return std::move(guard).value();
}
void BufferPoolManager::Release(page_id_t page_id, bool dirty) {
  std::lock_guard lock(mutex_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return;
  Frame& frame = frames_[it->second];
  if (frame.pin_count > 0) --frame.pin_count;
  frame.dirty = frame.dirty || dirty;
}
Status BufferPoolManager::UnpinPage(page_id_t page_id, bool dirty) {
  Release(page_id, dirty);
  return Status::Ok();
}
Status BufferPoolManager::FlushPage(page_id_t page_id) {
  std::lock_guard lock(mutex_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return NotFound("page is not cached");
  Frame& frame = frames_[it->second];
  TUPLESTONE_RETURN_IF_ERROR(disk_manager_->WritePage(page_id, frame.page));
  frame.dirty = false;
  return Status::Ok();
}
Status BufferPoolManager::FlushAllPages() {
  std::lock_guard lock(mutex_);
  for (Frame& frame : frames_)
    if (frame.page.id() != kInvalidPageId && frame.dirty) {
      TUPLESTONE_RETURN_IF_ERROR(disk_manager_->WritePage(frame.page.id(), frame.page));
      frame.dirty = false;
    }
  return Status::Ok();
}
Status BufferPoolManager::DeletePage(page_id_t page_id) {
  std::lock_guard lock(mutex_);
  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    Frame& frame = frames_[it->second];
    if (frame.pin_count != 0) return InvalidArgument("cannot delete a pinned page");
    frame.page.Reset();
    frame.dirty = false;
    page_table_.erase(it);
  }
  return disk_manager_->FreePage(page_id);
}
size_t BufferPoolManager::PinnedPages() const {
  std::lock_guard lock(mutex_);
  size_t count = 0;
  for (const Frame& frame : frames_)
    if (frame.pin_count != 0) ++count;
  return count;
}
}  // namespace tuplestone
