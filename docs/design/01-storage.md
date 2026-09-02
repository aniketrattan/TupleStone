# Storage primitives

`DiskManager` owns a 4 KiB page file with a versioned header, CRC32C checksums, a threaded free
list, and explicit flush support. `FaultDiskManager` can fail or tear deterministic writes. The
buffer pool adds bounded frames, pin counts, dirty tracking, eviction, and an RAII `PageGuard`.

The page-backed primitives are deliberately independent of the compact SQL serializer, so the
later page-format migration can be tested without changing the public API.
