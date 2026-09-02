# TupleStone — Project Plan

> An embedded, ACID, MVCC relational database with a real SQL front end, written from scratch in
> C++20 with no third-party runtime dependencies.

This is the **master roadmap**. It defines what gets built, in what order, and what "done" means for
each step. Companion documents:

- [ARCHITECTURE.md](ARCHITECTURE.md) — frozen technical decisions (file formats, protocols,
  invariants, SQL grammar). Read this before writing any code.
- [CONTRIBUTING.md](CONTRIBUTING.md) — rules of engagement for contributors.

---

## 1. Project identity

The project name is **TupleStone**. It is the current public identity for the namespace, library,
shell, documentation, and on-disk format.

| | |
|---|---|
| Name | `tuplestone` |
| Namespace | `tuplestone::` |
| Library | `libtuplestone` (static) |
| CLI binary | `tuplestone` |
| Language | C++20 |
| Build | CMake ≥ 3.25 + Ninja |
| Verified toolchain | CMake 4.4.2, g++ 14.2.0 (MSYS2 UCRT64), git, python3 |

> **Renaming:** a future product rename is a compatibility decision because the on-disk markers and
> public namespace are now established. Record one in an ADR before changing them.

### Current implementation checkpoint (2026-09-02)

The repository currently has a working vertical slice rather than a completed M0–M12 engine:

- M1/M2-style checksummed page, free-list, buffer-pool, heap, ordered-index, and transaction
  primitives are implemented and unit-tested.
- The public API has typed values, core DDL/DML, prepared parameters, expression evaluation,
  copy-on-write transactions, a CLI, and durable snapshot WAL/restart replay.
- The SQL path does **not** yet claim page-backed heap/index integration, tuple-version MVCC, or
  ARIES redo/undo. Index DDL returns `NotSupported` until it can be maintained correctly.
- The current evidence package is `docs/PORTFOLIO.md`, `tools/demo.ps1`, and the optional
  dependency-free `tuplestone_bench` target.

### What this is

A single-file-per-database embedded relational engine, in the shape of SQLite, with a roadmap toward
Postgres-style MVCC. You link `libtuplestone` into a program, or you drive it from the `tuplestone` shell.
The current vertical slice parses and evaluates a useful SQL subset over copy-on-write snapshots;
the page-backed heap/index path and tuple-version MVCC remain explicit follow-up milestones.

### Non-goals (do not build these)

Writing them down so no one scope-creeps into them:

- No networking, server daemon, or wire protocol.
- No replication, sharding, or distribution.
- No stored procedures, triggers, or views.
- No window functions, CTEs, or recursive queries.
- No users, roles, or permissions.
- No query/result caching.
- No cost-based join *reordering* — only the heuristics named in M10.

Each is a legitimate "future work" bullet for the README. None is in scope.

---

## 2. Milestones

Thirteen milestones, M0–M12. Each one names its goal, the modules it introduces, and an **exit
criterion**: a concrete test that must pass before the milestone is done. Each also requires a design
note at `docs/design/NN-<name>.md` recording what was built and anything surprising that was learned.

---

### M0 — Skeleton

**Goal:** a repository that builds, tests, and lints itself, plus the shared primitives every later
layer depends on.

**Introduces**
- `CMakeLists.txt`, `CMakePresets.json` with presets: `debug`, `release`, `asan`, `ubsan`, `tsan`.
- `FetchContent` for GoogleTest (test-only); the current benchmark smoke target is dependency-free.
- Warnings-as-errors: `-Wall -Wextra -Wpedantic -Werror` (`/W4 /WX` on MSVC).
- `.clang-format`, `.clang-tidy`, `.gitignore`, `.github/workflows/ci.yml`, `README.md`.
- `git init` and an initial commit.
- `src/common/`: `Status` / `StatusOr<T>`, `Slice`, `crc32c`, little-endian load/store helpers,
  a minimal logger, `TUPLESTONE_ASSERT` / `TUPLESTONE_PARANOID_ASSERT`.

**Exit criterion**
`ctest` is green under every preset with the `common` unit tests, and CI passes on a clean clone.

**Design note:** `docs/design/00-skeleton.md`

---

### M1 — Disk manager & pages

**Goal:** durable, checksummed, fixed-size page I/O against a single database file.

**Introduces**
- `src/disk/`: `Page` (4 KiB, `kPageSize`), `page_id_t` (`uint32_t`), `kInvalidPageId`.
- File header page (page 0): magic `TSTONE01`, format version, page count, free-list head,
  next txn id, checkpoint LSN. Layout is frozen in ARCHITECTURE.md.
- `DiskManager`: allocate / read / write / free a page, `Sync()` (real `fsync`/`FlushFileBuffers`),
  free-page list threaded through freed pages.
- `FaultDiskManager`: a test double that can fail, delay, or **tear** the Nth write. This is the
  foundation of the M6 crash tests — build it now, not later.

**Exit criterion**
Round-trip test: write N pages, close, reopen, read them all back identical. Free-list reuse test.
Torn-write test: a page with a corrupted body is detected by its CRC32C on read and returns a
`Status`, never garbage.

**Design note:** `docs/design/01-disk.md`

---

### M2 — Buffer pool

**Goal:** a bounded in-memory cache of pages with correct pinning, eviction, and latching.

**Introduces**
- `src/buffer/`: `BufferPoolManager`, frame table, page table (`page_id` → frame), pin counts,
  dirty flags.
- `LRUKReplacer` (K=2) for eviction.
- **`PageGuard`** — RAII handle that holds a pin *and* a read or write latch, releasing both on
  destruction. After this milestone, no code outside `buffer/` ever touches a raw `Page*`.
- `FetchPage`, `NewPage`, `FlushPage`, `FlushAll`, `DeletePage`.

**Exit criterion**
- Eviction correctness: with a pool of 10 frames, touching 1000 pages preserves all data.
- Pinned pages are never evicted; a full pool of pinned pages returns a `Status`, not a crash.
- 8-thread stress test clean under **TSan**.
- Leak check: after every test, all pin counts are zero.

**Design note:** `docs/design/02-buffer.md`

---

### M3 — Heap files & tuples

**Goal:** store variable-length rows in pages and address them stably.

**Introduces**
- `src/table/`: slotted page layout (header, slot array growing forward, tuple data growing
  backward, compaction on delete).
- Tuple format: header (`xmin`, `xmax`, next-version `RID`, flags), null bitmap, fixed-width fields
  inline, variable-length payload area with offset/length pairs. Layout frozen in ARCHITECTURE.md.
- `RID = { page_id_t page_id; uint16_t slot_id; }` — the stable row address that indexes point at.
- `TableHeap`: `InsertTuple`, `GetTuple`, `UpdateTuple`, `MarkDelete`, and a `TableIterator` for
  sequential scans.

> The tuple header already carries the MVCC fields. M3 leaves them at fixed sentinel values; M7 gives
> them meaning. Do **not** design a header without them and try to add them later.

**Exit criterion**
Round-trip of every supported type, including NULLs and empty strings, through insert → scan → read.
100k tuples inserted across many pages, then fully scanned in order, with no leaked space
(verify with a page-level space accounting check).

**Design note:** `docs/design/03-heap.md`

---

### M4 — Type system & value encoding

**Goal:** a value model with correct SQL semantics, and a key encoding that makes index comparisons
a `memcmp`.

**Introduces**
- `src/common/value.h`: `Value` over `NULL | BOOLEAN | INTEGER (int64) | REAL (double) | TEXT | BLOB`.
- Three-valued logic: `NULL` comparisons yield `UNKNOWN`; `NULL = NULL` is not true.
- Type coercion rules (INTEGER ↔ REAL promotion; everything else is an error, no implicit
  string↔number coercion).
- **Order-preserving memcomparable encoding**: signed integers with a flipped sign bit, doubles with
  the IEEE-754 total-order transform, TEXT/BLOB escaped so no encoding is a prefix of another, plus a
  NULL-ordering byte and an ASC/DESC flag. Multi-column keys concatenate.

**Exit criterion**
Property test over ≥1M random value pairs: `sign(memcmp(encode(a), encode(b))) == sign(compare(a, b))`
for every pair of same-typed values, including NULLs, infinities, NaN, and negative zero.
Round-trip test: `decode(encode(v)) == v`. Fuzz target on the decoder survives 1M execs.

**Design note:** `docs/design/04-types.md`

---

### M5 — B+Tree index

**Goal:** a concurrent, persistent, ordered index mapping encoded keys to `RID`s.

**Introduces**
- `src/index/`: B+Tree internal and leaf page layouts (frozen in ARCHITECTURE.md), variable-length
  keys, sibling pointers on leaves.
- `Insert` with node split and root growth; `Delete` with redistribute-then-merge and root shrink.
- `Search` (point) and `RangeIterator` (forward scan from a lower bound).
- **Latch crabbing** for concurrency: acquire the child latch before releasing the parent; take write
  latches only when the child may split/merge, otherwise optimistic descent with read latches.
- Unique and non-unique variants (non-unique appends the `RID` to the key to break ties).
- `Validate()` — a full-tree invariant checker: ordering, fanout bounds, sibling chain, key ranges,
  leaf depth uniformity.

**Exit criterion**
1M keys inserted, looked up, range-scanned, and deleted, single-threaded, with `Validate()` after
each phase. Then the same workload across 8 threads, clean under **TSan**, ending with a valid tree
containing exactly the expected key set.

**Design note:** `docs/design/05-btree.md`

---

### M6 — Write-ahead log & recovery

**Goal:** committed data survives a crash; uncommitted data never appears after one.

**Introduces**
- `src/wal/`: log record types `BEGIN`, `COMMIT`, `ABORT`, `INSERT`, `UPDATE`, `DELETE`, `NEW_PAGE`,
  `CHECKPOINT_BEGIN`, `CHECKPOINT_END`. Physiological logging (page id + slot + before/after image).
- Monotone `lsn_t`; every page carries the LSN of the last log record that modified it.
- **The WAL rule:** a dirty page is never written to disk before the log records describing it are
  fsync'd. Enforced in the buffer pool's flush path.
- Group commit: batch fsyncs across concurrent committers.
- Fuzzy checkpoints: record the dirty page table and active transaction table without quiescing.
- Restart recovery: analysis (rebuild DPT/ATT from the last checkpoint) → redo (replay forward,
  skipping pages whose LSN is already ahead) → undo (roll back losers).

**Exit criterion**
**The crash-injection suite.** A harness runs a mixed workload in a subprocess, kills it at a
randomized fsync/write point via `FaultDiskManager`, reopens the database, and asserts:
1. every transaction that returned "committed" is fully present,
2. no effect of any uncommitted transaction is visible,
3. all structural invariants (`Validate()` on every index, heap space accounting) hold.
CI runs a fixed seed corpus of ≥200 crash points; a soak target runs randomized seeds locally.

**Design note:** `docs/design/06-wal.md`

---

### M7 — Transactions & MVCC

**Goal:** concurrent readers and writers under snapshot isolation, with readers that never block.

**Introduces**
- `src/txn/`: `TransactionManager`, monotone `txn_id_t`, `Transaction` object holding its snapshot,
  write set, and undo information.
- **Snapshot** = `(xmin, xmax, active_set)` taken at transaction start.
- **Visibility rule** over the tuple header's `xmin`/`xmax`, walking the version chain via the
  next-version `RID` until a visible version is found. Exact rule in ARCHITECTURE.md.
- Update = insert a new version + link it into the chain + stamp the old version's `xmax`.
- Write-write conflict: **first updater wins**; the second updater aborts with a serialization error.
- `Rollback` restoring via the write set; `Commit` stamping the commit timestamp and logging.
- **Vacuum**: a background pass reclaiming versions invisible to every live snapshot.

**Exit criterion**
An isolation test battery driven by a deterministic multi-connection scheduler:
- no dirty reads, no non-repeatable reads, no phantoms within a snapshot,
- a reader running concurrently with a large writer sees a consistent point-in-time view,
- concurrent updates to the same row: exactly one commits,
- vacuum reclaims space and never removes a version a live snapshot can see.

**Write skew is permitted** — that is snapshot isolation working as specified, not a bug. Document it
in the design note and name SSI as future work.

**Design note:** `docs/design/07-mvcc.md`

---

### M8 — Catalog

**Goal:** the database describes its own schema, transactionally, in its own tables.

**Introduces**
- `src/catalog/`: system tables `tuplestone_tables`, `tuplestone_columns`, `tuplestone_indexes` — stored as
  ordinary heap tables with fixed, hardcoded `table_id`s, bootstrapped on first open.
- `Schema`, `Column`, `TableInfo`, `IndexInfo`.
- In-memory schema cache invalidated on DDL commit.
- DDL (`CREATE`/`DROP TABLE`, `CREATE`/`DROP INDEX`) executed inside a normal transaction, so it
  rolls back like anything else.

**Exit criterion**
Create tables and indexes, restart the process, and find the schemas intact. Abort a `CREATE TABLE`
and find no trace of it. Corrupt a catalog page and get a clean `Status`, not a crash.

**Design note:** `docs/design/08-catalog.md`

---

### M9 — SQL front end

**Goal:** turn SQL text into a typed, name-resolved logical plan — with good error messages.

**Introduces**
- `src/sql/lexer.*`: hand-written tokenizer (keywords, identifiers, quoted identifiers, string and
  numeric literals, operators, comments) tracking line:column.
- `src/sql/parser.*`: recursive-descent parser, precedence climbing for expressions, producing an AST
  for the grammar frozen in ARCHITECTURE.md.
- `src/sql/binder.*`: resolves table/column names against the catalog, assigns types, checks
  arity/type of functions and operators, expands `*`, and emits a typed **logical plan**.
- Errors carry position and a caret line: `line 1:23: unknown column "usr_id"; did you mean "user_id"?`

> This milestone has **no storage dependency** beyond the catalog. It can be built in parallel with
> M6/M7 if you want an early visible demo.

**Exit criterion**
- The full documented grammar parses; a corpus of ≥100 malformed statements each produce a specific,
  positioned error (golden-file test).
- The binder rejects unknown names, ambiguous names, and type errors.
- **libFuzzer target on `parse(input)` survives 1M execs with no crash, hang, or leak.**

**Design note:** `docs/design/09-sql.md`

---

### M10 — Planner & optimizer

**Goal:** choose a reasonable physical plan and be able to explain it.

**Introduces**
- `src/planner/`: logical → physical plan lowering.
- Rule-based rewrites, applied to fixpoint: constant folding, predicate pushdown into scans,
  projection pruning, `AND`-decomposition of filters, `IndexScan` selection from sargable predicates
  (`col op const` on an indexed prefix), `Limit` pushdown.
- Simple statistics: per-table tuple count and per-index distinct-key estimate, maintained on write
  and stored in the catalog.
- Join strategy heuristic only: hash join when there is an equality predicate, with the smaller
  estimated side as the build side; nested-loop otherwise. **No join reordering.**
- `EXPLAIN` rendering the physical plan tree with estimated cardinalities.

**Exit criterion**
Golden-file tests: for a fixed schema and ~30 queries, the `EXPLAIN` output matches expected plans —
index chosen where one applies, predicates pushed down, build side correct.

**Design note:** `docs/design/10-planner.md`

---

### M11 — Execution engine

**Goal:** actually run the queries.

**Introduces**
- `src/exec/`: Volcano/iterator model — every operator implements `Init()` / `Next(Tuple*, RID*)`.
- Operators: `SeqScan`, `IndexScan`, `Filter`, `Projection`, `NestedLoopJoin`, `HashJoin`,
  `Sort` (external merge sort spilling to temp files when it exceeds its memory budget),
  `Limit`/`Offset`, `Distinct`, `HashAggregate` (`COUNT`, `SUM`, `AVG`, `MIN`, `MAX`, `GROUP BY`,
  `HAVING`), `Insert`, `Update`, `Delete`.
- Expression evaluator over `Value` with the M4 three-valued semantics.
- Index maintenance on write: every `Insert`/`Update`/`Delete` updates all affected indexes within
  the same transaction.

**Exit criterion**
**The `.slt` suite.** `tests/slt/*.slt` — plain-text files of SQL statements and expected results, run
by a small harness (`tools/slt_runner`). This becomes the project's primary regression net; every
later change must keep it green. Cover: every operator, NULL handling, aggregates over empty inputs,
joins with no matches, sort spilling, and the update-then-query paths.

**Design note:** `docs/design/11-exec.md`

---

### M12 — Embedded API, CLI, and polish

**Goal:** something a person can actually use and a reviewer can actually evaluate.

**Introduces**
- `include/tuplestone/db.h` — the public surface, frozen back in M0 (see ARCHITECTURE.md):
  `Database`, `Connection`, `Transaction`, `PreparedStatement`, `ResultSet`. Exception-free,
  `Status`-based.
- `src/cli/`: the `tuplestone` REPL — multi-line statement accumulation, history, aligned result tables,
  and dot-commands: `.tables`, `.schema [table]`, `.indexes`, `.timer on|off`, `.explain`,
  `.import <csv> <table>`, `.dump`, `.read <file>`, `.help`, `.quit`.
- `tests/bench/`: the current dependency-free public-API smoke benchmark; the planned full suite will
  cover B+Tree point lookup, buffer-pool hits, tuple serialization, and macro workloads.
- `README.md`: architecture diagram, build instructions, a demo transcript, benchmark numbers, honest
  limitations, and future work.

**Exit criterion**
A `demo.sh` / `demo.ps1` that: creates a database, imports a real dataset (~1M rows), runs a set of
analytical queries with `.timer on`, is killed mid-write with `kill -9`, reopens, passes an integrity
check, and shows all committed data intact.

**Design note:** `docs/design/12-api-cli.md`

---

## 3. Sequencing

**Storage half — M0 → M5.** Everything here is about bytes on disk and correct concurrency. Do not
rush it: every bug in M2/M3/M5 will resurface as an unexplainable corruption during M6/M7.

**Engine half — M6 → M12.** M6 and M7 are the two hardest milestones in the project and they depend
on M3 and M5 being genuinely solid. If `Validate()` on the B+Tree is not exhaustive, fix that before
starting M6.

**Parallelizable:** M9 (SQL front end) depends only on M8's catalog interface, not on M6/M7. If you
want an impressive intermediate demo, build M9 early — a parser that produces plan trees is very
visible progress.

Rough effort weighting, for planning purposes: M5, M6, and M7 are each roughly as much work as M0–M4
combined. M11 is large but mostly mechanical once M10 is stable.

---

## 4. Testing strategy

Rigor is a requirement of this project, not a nice-to-have. A milestone is not done when the feature
works; it is done when the tests below cover it.

**Unit tests** — GoogleTest, at `tests/unit/<module>_test.cpp`. Every module ships its tests in the
same commit that introduces it.

**SQL logic tests** — `tests/slt/*.slt`, sqllogictest-style. From M11 onward this is the main
end-to-end regression net.

**Crash-injection tests** — `FaultDiskManager` (built in M1) plus a subprocess harness. CI runs a
fixed corpus of crash points; a soak target runs randomized seeds locally for hours.

**Fuzzing** — libFuzzer targets in `tests/fuzz/` with a checked-in corpus:
`fuzz_parser`, `fuzz_key_decode`, `fuzz_page_deserialize`, `fuzz_wal_record`. Any input that ever
crashed goes into the corpus permanently.

**Sanitizers** — ASan + UBSan on every CI run. TSan on the concurrency subset (buffer pool, B+Tree,
transaction manager). A TSan report is a build failure, never a "known flake".

**Invariant checkers** — `Validate()` on the B+Tree, heap pages, and the free list, compiled in under
`TUPLESTONE_PARANOID` and called after every mutation in debug tests.

**Benchmarks** — the current smoke benchmark is dependency-free and records its workload and machine
context in `docs/bench/`; a Google Benchmark suite is a later performance milestone.

**CI matrix** — Linux (GCC + Clang) and Windows (MSYS2 UCRT64). Build all presets, run `ctest`, run
each fuzzer for 60 s as a smoke test, check `clang-format`.

---

## 5. Reference reading

- **CMU 15-445/645 Database Systems** — lectures and the BusTub assignments. The single best match
  for this project's shape.
- **SQLite** — the [file format](https://sqlite.org/fileformat2.html) and
  [WAL](https://sqlite.org/wal.html) docs; the clearest published description of a real embedded
  engine's on-disk layout.
- **ARIES** (Mohan et al., 1992) — the recovery algorithm M6 is modeled on.
- **The LRU-K page replacement algorithm** (O'Neil et al., 1993) — M2's replacer.
- **PostgreSQL internals** — heap tuple layout, MVCC visibility rules, and vacuum. M3 and M7 follow
  this model.
- *Database Internals*, Alex Petrov — best single book for M1–M6.
- *Architecture of a Database System*, Hellerstein, Stonebraker & Hamilton — the overall map.
- *Readings in Database Systems* ("the Red Book") — for the design-decision rationale.
