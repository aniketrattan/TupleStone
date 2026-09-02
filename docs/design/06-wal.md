# Durable commit log and restart replay

The public SQL path now uses the WAL for every committed mutation. This is a deliberately bounded
step toward the ARIES design in `ARCHITECTURE.md`; it is not presented as a full physiological WAL
or page-level recovery implementation yet.

## Commit protocol

The compact engine keeps its logical tables in memory and writes an atomic snapshot to the data
file. Before that replacement, it appends three checksummed records to `<database>.wal`:

1. `BEGIN` with a monotonically increasing transaction id.
2. `UPDATE` whose payload is a versioned serialized table snapshot.
3. `COMMIT`, followed by `Flush()`/`fsync` when `Options::sync_on_commit` is enabled.

Only a snapshot with a matching `COMMIT` is eligible for replay. A process that dies after the
commit record is durable but before the data-file rename therefore recovers the committed state;
an incomplete transaction has no replayable snapshot.

`Database::Checkpoint()` first writes the current snapshot and then truncates the log. If a crash
happens between those operations, replaying the already-installed snapshot is harmless.

## Tail handling

WAL records are length-prefixed and CRC32C-protected. Recovery accepts the valid prefix of a log
and ignores a partially written final record, which is the expected result of a torn append. A
malformed committed snapshot still returns `Corruption` rather than silently producing data.

## Verification

`EngineTest.ReplaysCommittedSnapshotWhenDataFileIsLost` removes the data file after a committed
session and verifies that reopening from the surviving WAL restores the row. The storage tests also
exercise record round-tripping and checksums.

## Boundary

The next recovery milestone remains the full page-oriented ARIES path: physiological before/after
images, page LSNs, dirty-page and active-transaction tables, redo/undo, CLRs, and crash injection
against the buffer pool. Until that work is complete, this component should be described as
**snapshot WAL/restart replay**, not as ARIES recovery.
