#include "buffer/lruk_replacer.h"
#include <limits>
namespace tuplestone {
void LRUKReplacer::RecordAccess(size_t frame_id) {
  std::lock_guard lock(mutex_);
  if (entries_.size() >= capacity_ && !entries_.contains(frame_id)) return;
  entries_[frame_id].last = ++clock_;
}
void LRUKReplacer::SetEvictable(size_t frame_id, bool evictable) {
  std::lock_guard lock(mutex_);
  auto& entry = entries_[frame_id];
  entry.evictable = evictable;
  if (entry.last == 0) entry.last = ++clock_;
}
std::optional<size_t> LRUKReplacer::Evict() {
  std::lock_guard lock(mutex_);
  size_t victim = 0;
  uint64_t oldest = std::numeric_limits<uint64_t>::max();
  bool found = false;
  for (const auto& [frame, entry] : entries_)
    if (entry.evictable && entry.last < oldest) {
      oldest = entry.last;
      victim = frame;
      found = true;
    }
  if (!found) return std::nullopt;
  entries_.erase(victim);
  return victim;
}
void LRUKReplacer::Remove(size_t frame_id) {
  std::lock_guard lock(mutex_);
  entries_.erase(frame_id);
}
size_t LRUKReplacer::Size() const {
  std::lock_guard lock(mutex_);
  size_t size = 0;
  for (const auto& [frame, entry] : entries_) {
    (void)frame;
    if (entry.evictable) ++size;
  }
  return size;
}
}  // namespace tuplestone
