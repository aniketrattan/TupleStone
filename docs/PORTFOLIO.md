# Portfolio showcase brief

TupleStone is best presented as a compact embedded SQL engine vertical slice while the roadmap
continues toward a page-backed MVCC database.

## What can be demonstrated today

- C++20 library plus a small command-line shell.
- `CREATE TABLE`, `INSERT`, `SELECT`, `UPDATE`, `DELETE`, `ORDER BY`, `LIMIT`, `DISTINCT`, typed
  expressions, prepared parameters, and explicit transactions.
- Single-file persistence with atomic replacement and a checksummed `<database>.wal` commit log.
- Restart replay of a committed snapshot when the data file is unavailable.
- Checksummed page I/O, free-page reuse, bounded buffer-pool primitives, ordered-index primitives,
  and transaction/catalog layers that are being integrated incrementally.
- 72 unit tests passing in the verified Windows debug/release configurations, with the same test
  suite wired into the Linux sanitizer CI matrix.

## Suggested two-minute demo

```text
CREATE TABLE projects (id INTEGER PRIMARY KEY, name TEXT NOT NULL, stars INTEGER);
INSERT INTO projects VALUES (1, 'storage', 42), (2, 'recovery', 57), (3, 'sql', NULL);
SELECT id, name, stars FROM projects WHERE stars IS NOT NULL ORDER BY stars DESC;
BEGIN;
INSERT INTO projects VALUES (4, 'uncommitted', 0);
ROLLBACK;
SELECT COUNT(*) FROM projects;
.schema projects
```

Run the same flow with `tools/demo.ps1`, then reopen the database to show that the committed rows
survive. Run `tuplestone_bench` from the release build to report a reproducible insert/query baseline.

## Accurate project description

> TupleStone: a C++20 embedded SQL engine prototype with typed values, prepared statements,
> transactional snapshots, atomic single-file persistence, checksummed page/WAL primitives, and a
> scriptable CLI. Current engineering work is integrating page-backed heap/index storage and
> ARIES-style recovery; those are explicitly not claimed as complete yet.

## Reviewer questions to answer

1. What survives a process kill? A committed snapshot whose WAL commit record was flushed; an
   incomplete transaction is ignored during replay.
2. Is this full ARIES? No. The current WAL is snapshot-level restart replay; page-oriented redo/undo
   is the next milestone.
3. What is the isolation model? Copy-on-write snapshots with first-committer-wins generation
   conflicts, not tuple-version MVCC yet.
4. How is correctness measured? Unit tests cover values, page checksums, WAL records, persistence,
   prepared parameters, rollback, and restart replay; CI adds sanitizer and formatting gates.

Depth is more valuable than checking every roadmap box. The next flagship tranche should make one
subsystem genuinely page-backed end-to-end, then add a benchmark and crash corpus around it.
