// CRC32C (Castagnoli, polynomial 0x1EDC6F41) — the checksum ARCHITECTURE.md §3
// specifies for page bodies and §4.7 for WAL records.
//
// Chosen over CRC32 (zlib) because it detects a strictly larger class of burst
// errors at the sizes we care about, and because it is the same choice SQLite's
// successors, LevelDB, and iSCSI made — a torn 4 KiB page is exactly the failure
// it was designed for.
#ifndef TUPLESTONE_COMMON_CRC32C_H_
#define TUPLESTONE_COMMON_CRC32C_H_

#include <cstddef>
#include <cstdint>

#include "common/slice.h"

namespace tuplestone {

// Standard framing: pre- and post-invert, reflected input and output.
uint32_t Crc32c(const uint8_t* data, size_t size);
inline uint32_t Crc32c(const Slice& s) {
  return Crc32c(s.data(), s.size());
}

// Continues a checksum over a further chunk. Feeding a buffer in any split
// produces the same result as one call over the whole buffer:
//   Crc32cExtend(Crc32c(a), b) == Crc32c(a ++ b)
uint32_t Crc32cExtend(uint32_t prior_crc, const uint8_t* data, size_t size);

}  // namespace tuplestone

#endif  // TUPLESTONE_COMMON_CRC32C_H_
