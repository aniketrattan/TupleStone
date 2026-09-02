#ifndef TUPLESTONE_DISK_PAGE_H_
#define TUPLESTONE_DISK_PAGE_H_

#include <array>
#include <cstdint>

#include "common/types.h"

namespace tuplestone {

enum class PageType : uint8_t {
  kFree = 4,
  kHeap = 1,
  kBtreeInternal = 2,
  kBtreeLeaf = 3,
  kOverflow = 5
};

class Page {
 public:
  Page() { Reset(); }
  explicit Page(page_id_t id) {
    Reset();
    page_id_ = id;
  }

  page_id_t id() const { return page_id_; }
  void set_id(page_id_t id) { page_id_ = id; }
  uint8_t* data() { return bytes_.data(); }
  const uint8_t* data() const { return bytes_.data(); }
  static constexpr size_t Size() { return kPageSize; }
  void Reset() {
    bytes_.fill(0);
    page_id_ = kInvalidPageId;
  }

 private:
  page_id_t page_id_ = kInvalidPageId;
  std::array<uint8_t, kPageSize> bytes_{};
};

}  // namespace tuplestone

#endif
