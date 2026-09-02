#ifndef TUPLESTONE_BUFFER_LRUK_REPLACER_H_
#define TUPLESTONE_BUFFER_LRUK_REPLACER_H_
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>
namespace tuplestone {
class LRUKReplacer {
 public:
  LRUKReplacer(size_t capacity, size_t k = 2) : capacity_(capacity), k_(k == 0 ? 1 : k) {}
  void RecordAccess(size_t frame_id);
  void SetEvictable(size_t frame_id, bool evictable);
  std::optional<size_t> Evict();
  void Remove(size_t frame_id);
  size_t Size() const;

 private:
  struct Entry {
    std::deque<uint64_t> accesses;
    bool evictable = false;
  };
  size_t capacity_;
  size_t k_;
  mutable std::mutex mutex_;
  uint64_t clock_ = 0;
  std::unordered_map<size_t, Entry> entries_;
};
}  // namespace tuplestone
#endif
