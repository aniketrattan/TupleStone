// The identifier types every layer above `common` shares. ARCHITECTURE.md §3.
#ifndef NANOSQL_COMMON_TYPES_H_
#define NANOSQL_COMMON_TYPES_H_

#include <cstddef>
#include <cstdint>

namespace nanosql {

using page_id_t = uint32_t;
using slot_id_t = uint16_t;
using lsn_t = uint64_t;
using txn_id_t = uint64_t;
using table_id_t = uint32_t;
using index_id_t = uint32_t;

constexpr page_id_t kInvalidPageId = 0xFFFFFFFFu;
constexpr lsn_t kInvalidLsn = 0;
constexpr txn_id_t kInvalidTxnId = 0;

// Not configurable, by decision: ARCHITECTURE.md §3.
constexpr size_t kPageSize = 4096;
constexpr uint8_t kPageSizeLog2 = 12;
static_assert(kPageSize == (size_t{1} << kPageSizeLog2));

// Page 0 is always the file header, so it is never a valid data page id.
constexpr page_id_t kHeaderPageId = 0;

// A stable row address. This is what index leaves store, and what survives page
// compaction — see ARCHITECTURE.md §4.3 on why slot indices are never reused.
// Serialized as 6 bytes: page_id (4 LE) then slot_id (2 LE).
struct RID {
  page_id_t page_id = kInvalidPageId;
  slot_id_t slot_id = 0;

  bool valid() const { return page_id != kInvalidPageId; }

  friend bool operator==(const RID& a, const RID& b) {
    return a.page_id == b.page_id && a.slot_id == b.slot_id;
  }
  friend bool operator!=(const RID& a, const RID& b) { return !(a == b); }
  friend bool operator<(const RID& a, const RID& b) {
    if (a.page_id != b.page_id) return a.page_id < b.page_id;
    return a.slot_id < b.slot_id;
  }
};

constexpr size_t kRidSerializedSize = 6;

}  // namespace nanosql

#endif  // NANOSQL_COMMON_TYPES_H_
