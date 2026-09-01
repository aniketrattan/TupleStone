# nanosql — Architecture

**This document is the frozen technical design.** Its job is to stop every future contributor from
re-litigating decisions that have already been made. If you are implementing a milestone from
[PLAN.md](PLAN.md), the formats, protocols, and invariants you need are here — you should never have
to invent a page layout or a log record format yourself.

Changing anything in this document requires an ADR in `docs/adr/NNNN-<title>.md` and explicit human
approval. See [CONTRIBUTING.md](CONTRIBUTING.md).

---

## 1. Layer map

Dependencies point strictly **downward**. A layer may never include a header from a layer above it.
This is checked by review and, from M2 onward, by a CMake target-link graph that makes upward
dependencies fail to link.

```
                    cli          (REPL, dot-commands)
                     ↓
                    api          (Database, Connection, ResultSet)
                     ↓
                    exec         (Volcano operators, expression evaluator)
                     ↓
                   planner       (logical → physical, rewrite rules, EXPLAIN)
                     ↓
                    sql          (lexer, parser, binder)
                     ↓
                  catalog        (system tables, Schema, TableInfo)
                     ↓
                    txn          (TransactionManager, snapshots, visibility, vacuum)
                     ↓
            index  ←→  table     (B+Tree)      (TableHeap, slotted pages, tuples)
                     ↓
                    wal          (log records, LSN, recovery)
                     ↓
                   buffer        (BufferPoolManager, LRU-K, PageGuard)
                     ↓
                    disk         (DiskManager, Page, file header, free list)
                     ↓
                   common        (Status, Slice, Value, encoding, crc32c, logging)
```

`index` and `table` are peers: neither includes the other. The `RID` type they share lives in
`common`.

---

## 2. Directory layout

```
d:\Database\
├─ PLAN.md ARCHITECTURE.md CONTRIBUTING.md README.md
├─ CMakeLists.txt  CMakePresets.json  .clang-format  .clang-tidy  .gitignore
├─ include/nanosql/          # PUBLIC headers only — the embedded API surface
│    db.h  status.h  value.h
├─ src/
│    common/  disk/  buffer/  wal/  table/  index/  txn/
│    catalog/ sql/  planner/  exec/  api/  cli/
├─ tests/
│    unit/                   # <module>_test.cpp, GoogleTest
│    slt/                    # *.slt sqllogictest-style files
│    fuzz/                   # libFuzzer targets + corpus/
│    crash/                  # crash-injection harness
│    bench/                  # Google Benchmark
├─ tools/                    # slt_runner, crash_harness, csv loader
└─ docs/
     design/                 # NN-<name>.md, one per milestone
     adr/                    # NNNN-<title>.md, architecture decision records
     bench/                  # benchmark results over time
```

Each `src/<layer>/` is one CMake target named `nanosql_<layer>`, linked into `libnanosql`.

---

## 3. Global conventions

- **Endianness:** all on-disk integers are **little-endian**, written through explicit
  `LoadU32LE` / `StoreU32LE` helpers in `common`. Never `memcpy` a struct to disk.
- **Padding:** every on-disk layout is specified byte-for-byte below. No implicit struct padding is
  relied upon; every reserved byte is explicit and zero-filled.
- **Alignment:** on-disk structures are read field-by-field, so no alignment assumptions are made.
- **Checksums:** CRC32C over the page body (everything after the checksum field).
- **Page size:** `kPageSize = 4096`, compile-time constant, not configurable.
- **Format version:** one byte in the file header. On open, a version the binary does not recognize
  is a hard error — `Status::Incompatible`. No silent upgrade, ever.

### Core types (`common`)

```cpp
using page_id_t = uint32_t;   constexpr page_id_t kInvalidPageId = 0xFFFFFFFFu;
using slot_id_t = uint16_t;
using lsn_t     = uint64_t;   constexpr lsn_t     kInvalidLsn    = 0;
using txn_id_t  = uint64_t;   constexpr txn_id_t  kInvalidTxnId  = 0;
using table_id_t = uint32_t;
using index_id_t = uint32_t;

struct RID {                  // stable row address; what indexes store
  page_id_t page_id;
  slot_id_t slot_id;
};
```

---

## 4. On-disk formats

### 4.1 File header — page 0

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | Magic `"NANOSQL1"` (ASCII, no NUL) |
| 8 | 1 | Format version (currently `1`) |
| 9 | 1 | Page size log2 (currently `12`) |
| 10 | 2 | Reserved (zero) |
| 12 | 4 | Page count |
| 16 | 4 | Free-list head `page_id` (`kInvalidPageId` if empty) |
| 20 | 8 | Next `txn_id` to hand out |
| 28 | 8 | Last checkpoint LSN |
| 36 | 4 | Next `table_id` |
| 40 | 4 | Next `index_id` |
| 44 | 4 | Catalog root: `nanosql_tables` first page |
| 48 | 4 | Catalog root: `nanosql_columns` first page |
| 52 | 4 | Catalog root: `nanosql_indexes` first page |
| 56 | 4036 | Reserved (zero) |
| 4092 | 4 | CRC32C of bytes 0..4091 |

The header page is written with an fsync barrier before and after. It is the only page in the file
that is not managed by the buffer pool — the `DiskManager` owns it directly.

### 4.2 Common page header — bytes 0..15 of every non-header page

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | CRC32C of bytes 4..4095 |
| 4 | 1 | Page type: `1` heap, `2` btree-internal, `3` btree-leaf, `4` free, `5` overflow |
| 5 | 1 | Reserved (zero) |
| 6 | 2 | Reserved (zero) |
| 8 | 8 | `page_lsn` — LSN of the last log record that modified this page |

`page_lsn` is what makes redo idempotent: during recovery, a log record whose LSN is ≤ the page's
`page_lsn` has already been applied and is skipped.

### 4.3 Slotted heap page — type 1

```
+----------------------------------------------------------+
| common header (16 B)                                      |
| next_page_id (4) | prev_page_id (4) | table_id (4)        |
| slot_count (2)   | free_ptr (2)     | free_space (2) | pad |
+----------------------------------------------------------+
| slot[0] | slot[1] | ... | slot[n-1]   →  grows forward     |
|                                                            |
|                  ← free space →                            |
|                                                            |
|                        ... tuple[n-1] | tuple[0]           |
|                                        ←  grows backward   |
+----------------------------------------------------------+
```

Each slot is 4 bytes: `offset (2) | length (2)`. `length == 0` means the slot is dead (its tuple was
compacted away); the slot index is never reused while any index may still point at it, which keeps
`RID`s stable. Compaction moves live tuples toward the page end and rewrites slot offsets, but never
changes slot *indices*.

### 4.4 Tuple format

```
+--------------------------------------------------------------+
| xmin (8) | xmax (8) | next_version RID (6) | flags (2)        |  header, 24 B
+--------------------------------------------------------------+
| null bitmap: ceil(column_count / 8) bytes                     |
+--------------------------------------------------------------+
| fixed-width column values, in schema order                    |
|   BOOLEAN 1 B | INTEGER 8 B | REAL 8 B                        |
|   TEXT/BLOB → 4 B offset + 4 B length into the payload area   |
+--------------------------------------------------------------+
| variable-length payload (TEXT/BLOB bytes, concatenated)       |
+--------------------------------------------------------------+
```

`flags` bit 0 = deleted, bit 1 = has-overflow-payload, bits 2..15 reserved.

**The MVCC header fields exist from M3 onward.** M3 writes `xmin = kInvalidTxnId`,
`xmax = kInvalidTxnId`, `next_version = kInvalidPageId`; M7 gives them meaning. Do not design the
tuple without them.

A tuple larger than roughly one third of a page moves its variable-length payload to a chain of
overflow pages (type 5); the inline payload area then holds a `page_id`/length pair and the
has-overflow flag is set.

### 4.5 B+Tree internal page — type 2

```
| common header (16 B) | parent_page_id (4) | key_count (2) | level (2) | pad (4) |
| slot[0..n-1] : key_offset (2) | key_len (2)      → grows forward    |
| child[0..n]  : page_id (4)  — n+1 children, stored after slots      |
|                          ← free space →                             |
|                     ... key bytes, growing backward                 |
```

Internal node with `n` keys has `n+1` children: `child[i]` holds keys `< key[i]`, `child[n]` holds
keys `≥ key[n-1]`.

### 4.6 B+Tree leaf page — type 3

```
| common header (16 B) | parent_page_id (4) | key_count (2) | level (=0, 2) |
| next_leaf (4) | prev_leaf (4)                                             |
| slot[0..n-1] : key_offset (2) | key_len (2) | RID (6)   → grows forward    |
|                          ← free space →                                    |
|                     ... key bytes, growing backward                        |
```

Keys are **memcomparable-encoded** (§6), so all comparisons inside the tree are `memcmp`. The tree
knows nothing about SQL types. For a non-unique index, the `RID` is appended to the encoded key
before insertion, making every key unique and making "delete this exact entry" unambiguous.

Fanout target: nodes split at ≥ 95% full and merge below 40% occupancy, with redistribution from a
sibling tried before merging.

### 4.7 WAL record format

The log is a separate file, `<db>.wal`, written sequentially and never randomly overwritten.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Total record length |
| 4 | 8 | `lsn` |
| 12 | 8 | `txn_id` |
| 20 | 8 | `prev_lsn` — previous record of this transaction (`kInvalidLsn` for BEGIN) |
| 28 | 1 | Record type |
| 29 | 3 | Reserved (zero) |
| 32 | … | Type-specific payload |
| end | 4 | CRC32C of the whole record |

Record types:

| Type | Payload |
|---|---|
| `BEGIN` (1) | — |
| `COMMIT` (2) | commit timestamp (8) |
| `ABORT` (3) | — |
| `INSERT` (4) | `page_id`(4) `slot_id`(2) `tuple_len`(2) `tuple bytes` |
| `UPDATE` (5) | `page_id`(4) `slot_id`(2) `before_len`(2) `after_len`(2) `before` `after` |
| `DELETE` (6) | `page_id`(4) `slot_id`(2) `tuple_len`(2) `tuple bytes` (the full before-image) |
| `NEW_PAGE` (7) | `page_id`(4) `page_type`(1) `prev_page_id`(4) |
| `CLR` (8) | compensation record: `undo_next_lsn`(8) + an INSERT/UPDATE/DELETE payload |
| `CKPT_BEGIN` (9) | — |
| `CKPT_END` (10) | active transaction table + dirty page table, serialized |

A partially written trailing record (bad length or bad CRC) marks the end of the valid log during
recovery — everything from there on is discarded. This is how a crash mid-append is handled.

---

## 5. Storage model — decisions

### 5.1 Heap files + secondary B+Tree indexes (Postgres model)

Rows live in heap pages, addressed by `RID`. Every index — including the primary key's — is a
separate B+Tree mapping an encoded key to a `RID`. A primary key lookup is therefore
index probe → heap fetch.

*Rejected: SQLite-style clustered storage* (rows stored in the primary-key B+Tree). It saves one
indirection for PK lookups, but every secondary index must then store variable-length primary keys
instead of fixed 6-byte `RID`s, and — decisively — MVCC version chains become far harder, because a
version chain would have to live inside a structure that also reorders itself on split. The heap
model keeps versions in a place that does not move.

**Consequence to remember:** an `UPDATE` writes a *new* heap tuple, so **every index on the table must
be updated**, even for columns the update did not touch. This is Postgres's well-known write
amplification. Accept it; do not invent HOT updates in this project.

### 5.2 MVCC — versions in the heap

A tuple version is visible to a transaction with snapshot `S = (xmin_s, xmax_s, active)` iff:

```
visible(t) :=
     committed_before_snapshot(t.xmin)
  && !committed_before_snapshot(t.xmax)          // xmax unset, or its txn is not visible-as-done

committed_before_snapshot(x) :=
     x != kInvalidTxnId
  && (x == self.txn_id                            // our own writes are visible to us
      || (x < xmax_s && !active.contains(x) && commit_status(x) == COMMITTED))
```

An `UPDATE` by transaction `T`:
1. Insert the new version with `xmin = T`, `xmax = kInvalidTxnId`.
2. Set the old version's `xmax = T` and its `next_version` to the new version's `RID`.
3. Log an `UPDATE` record covering both page modifications.

Reads walk the chain from the heap tuple forward until they find a visible version, or fall off the
end (row not visible).

*Rejected: MySQL/InnoDB-style undo-log versioning.* It uses space better and keeps the main heap
compact, but it couples version reconstruction to the undo log and makes rollback, purge, and reads
all depend on one another. The heap model is the one you can debug by dumping a page.

### 5.3 Isolation level: snapshot isolation, and only that

Readers never block writers; writers never block readers. Write-write conflicts abort the second
writer (**first updater wins**) with `Status::SerializationFailure`.

**Write skew is possible.** Two transactions can each read a set, each update a *different* row in it,
and both commit, violating a constraint neither could have violated alone. This is snapshot isolation
behaving as specified, not a defect. Document it in the README's limitations section. Serializable
Snapshot Isolation (SSI) is named as future work and is explicitly out of scope.

### 5.4 Vacuum

A version is dead when its `xmax` is committed and less than the `xmin` of the oldest live snapshot.
A background vacuum pass sweeps heap pages, removes dead versions, compacts pages, and deletes the
corresponding index entries. It takes page write latches one page at a time and never holds a latch
across pages.

---

## 6. Value and key encoding

### 6.1 Value semantics

`Value` is `NULL | BOOLEAN | INTEGER(int64_t) | REAL(double) | TEXT | BLOB`.

- Comparisons involving `NULL` yield `UNKNOWN`; `WHERE` treats `UNKNOWN` as false.
- `INTEGER` and `REAL` compare and arithmetic-combine after promoting to `REAL`.
- **No implicit string↔number coercion.** `'1' = 1` is a type error, not `true`. (This is
  deliberately unlike SQLite's type affinity; strictness is easier to reason about and to test.)
- Integer overflow is an error (`Status::OutOfRange`), not wraparound.
- Division by zero is an error, not `NULL`.

### 6.2 Memcomparable key encoding

The B+Tree compares keys with `memcmp`. The encoding must therefore be **order-preserving**:

- **Ordering prefix byte** per key column: `0x00` = NULL, `0x01` = non-NULL. NULLs sort first in
  `ASC`. For `DESC` columns, every byte of that column's encoding is bitwise-inverted after encoding,
  which reverses its order including the NULL marker.
- **BOOLEAN:** one byte, `0x00`/`0x01`.
- **INTEGER:** 8-byte big-endian with the sign bit flipped (`value ^ 0x8000000000000000`). This maps
  the signed range onto unsigned order.
- **REAL:** IEEE-754 total order transform — if the sign bit is set, invert all 64 bits; otherwise
  flip only the sign bit. Then store big-endian. `NaN` sorts last; `-0.0` and `+0.0` encode
  identically (they compare equal in SQL).
- **TEXT / BLOB:** encoded in 9-byte groups — 8 payload bytes plus one marker byte holding the number
  of real bytes in that group (`0xFF` if the group is full and more follows). This guarantees no
  encoding is a prefix of another, so `"ab"` sorts before `"abc"` correctly.
- **Composite keys** are the concatenation of their columns' encodings, in index order.

The M4 property test (`sign(memcmp(encode(a), encode(b))) == sign(compare(a, b))`) is the definition
of correctness here. If it passes on a million random pairs, the encoding is right.

---

## 7. Concurrency protocol

### 7.1 Latch ordering — non-negotiable

Deadlock among latches is prevented structurally, not detected. Every code path acquires latches in
this order and never the reverse:

```
buffer pool page-table latch  →  page latch  →  log latch
```

Additional rules:

1. **B+Tree crabbing:** descend top-down only, acquiring the child latch before releasing the parent.
   Never latch a parent while holding a child; never latch a right sibling while holding a left one
   in the opposite direction from the leaf chain order (leaf scans go left→right only).
2. **Never hold a page latch while acquiring a transaction-manager lock.** Copy what you need out of
   the page, drop the latch, then take the txn lock.
3. **Never hold two page latches from different structures** (e.g. a heap page and an index page) at
   once. Do the heap operation, release, then do the index operation.
4. A `PageGuard` must not outlive the function that created it, and must never be stored in a member.

Violations of rule 1 or 2 show up as TSan reports or as production hangs. Both are build failures.

### 7.2 WAL protocol

- **WAL rule:** before the buffer pool writes a dirty page to disk, the log must be flushed through
  that page's `page_lsn`. Enforced inside `BufferPoolManager::FlushPage` — there is exactly one place
  that can violate this, and it is guarded there.
- **Commit:** append `COMMIT`, flush the log through that LSN (fsync), *then* return success. Data
  pages need not be written.
- **Group commit:** concurrent committers queue; one thread performs the fsync and wakes the rest.
- **Fuzzy checkpoint:** write `CKPT_BEGIN`, snapshot the dirty page table and active transaction
  table, write `CKPT_END` containing them, update the header's checkpoint LSN. No transactions are
  quiesced.

### 7.3 Recovery — three passes

```
ANALYSIS  from the last CKPT_END: rebuild the dirty page table (DPT) and
          active transaction table (ATT). Transactions with COMMIT are winners;
          those still in the ATT at end-of-log are losers.
REDO      from min(recLSN in DPT) forward: for each redoable record, fetch the page;
          if page_lsn >= record.lsn, skip (already applied); else apply and set page_lsn.
          Redo is applied for winners AND losers — repeat history.
UNDO      for each loser, walk prev_lsn backward, applying the inverse of each change,
          writing a CLR for each undo step with undo_next_lsn = the record's prev_lsn,
          so a crash during recovery does not redo the undo work.
```

Recovery is idempotent: crashing during recovery and restarting must produce the same final state.
The M6 crash suite tests exactly this by injecting a second crash during the recovery pass.

---

## 8. Error handling

- **No exception ever crosses the public API.** `libnanosql` is built with exceptions enabled (STL
  needs them) but every public entry point catches `std::bad_alloc` and translates it.
- `Status` for recoverable failures; `StatusOr<T>` when a value is returned.
  Codes: `Ok, NotFound, AlreadyExists, InvalidArgument, SyntaxError, TypeError, IoError, Corruption,
  Incompatible, OutOfMemory, OutOfRange, SerializationFailure, NotSupported, Internal`.
- `Status` carries a message and, for SQL errors, a `line:column` position.
- **`NANOSQL_ASSERT` is for invariants that a bug would break** — a violated internal contract. It is
  active in all builds including release. `NANOSQL_PARANOID_ASSERT` wraps expensive checks
  (`Validate()` calls) and is compiled out unless `NANOSQL_PARANOID` is set.
- The division of labor: **I/O errors, corruption, and resource exhaustion are `Status`.
  Logic bugs are asserts.** Never return a `Status` for something that can only happen if the code is
  wrong, and never assert on something the environment can cause.

---

## 9. SQL subset

Everything outside this list is out of scope until the whole list works.

**Statements**
`CREATE TABLE`, `DROP TABLE`, `CREATE [UNIQUE] INDEX`, `DROP INDEX`, `INSERT INTO … VALUES`,
`INSERT INTO … SELECT`, `SELECT`, `UPDATE`, `DELETE`, `BEGIN`, `COMMIT`, `ROLLBACK`, `EXPLAIN`.

**SELECT features**
`WHERE`, `INNER`/`LEFT` `JOIN … ON`, comma joins, `GROUP BY`, `HAVING`, `ORDER BY … [ASC|DESC]`,
`LIMIT`/`OFFSET`, `DISTINCT`, table aliases, column aliases, subqueries in `FROM`, scalar subqueries,
`IN (subquery)`, `EXISTS`.

**Types** `INTEGER`, `REAL`, `TEXT`, `BOOLEAN`, `BLOB`.
**Constraints** `NOT NULL`, `PRIMARY KEY`, `UNIQUE`. (No `FOREIGN KEY`, no `CHECK`, no `DEFAULT`
expressions beyond literals.)

**Expressions** literals, column refs, `+ - * / %`, `= != < <= > >=`, `AND OR NOT`, `IS [NOT] NULL`,
`BETWEEN`, `IN (list)`, `LIKE`, `CAST(x AS type)`, `CASE WHEN`, parenthesization.
**Functions** `COUNT`, `SUM`, `AVG`, `MIN`, `MAX`, `ABS`, `LENGTH`, `UPPER`, `LOWER`, `SUBSTR`,
`COALESCE`.

### Grammar (EBNF, abridged to the shapes the parser must accept)

```ebnf
statement    = create_table | drop_table | create_index | drop_index
             | insert | select | update | delete | txn_stmt | explain ;

create_table = "CREATE" "TABLE" [ "IF" "NOT" "EXISTS" ] ident
               "(" column_def { "," column_def } [ "," table_constraint ] ")" ;
column_def   = ident type_name { column_constraint } ;
type_name    = "INTEGER" | "REAL" | "TEXT" | "BOOLEAN" | "BLOB" ;
column_constraint = "NOT" "NULL" | "PRIMARY" "KEY" | "UNIQUE" ;

create_index = "CREATE" [ "UNIQUE" ] "INDEX" ident "ON" ident "(" ident { "," ident } ")" ;

insert       = "INSERT" "INTO" ident [ "(" ident { "," ident } ")" ]
               ( "VALUES" value_list { "," value_list } | select ) ;

select       = "SELECT" [ "DISTINCT" ] select_list
               [ "FROM" from_item { "," from_item } ]
               [ "WHERE" expr ] [ "GROUP" "BY" expr { "," expr } ] [ "HAVING" expr ]
               [ "ORDER" "BY" order_item { "," order_item } ]
               [ "LIMIT" expr [ "OFFSET" expr ] ] ;
from_item    = ( ident | "(" select ")" ) [ [ "AS" ] ident ]
               { [ "INNER" | "LEFT" [ "OUTER" ] ] "JOIN" from_item "ON" expr } ;

update       = "UPDATE" ident "SET" assign { "," assign } [ "WHERE" expr ] ;
delete       = "DELETE" "FROM" ident [ "WHERE" expr ] ;
txn_stmt     = "BEGIN" | "COMMIT" | "ROLLBACK" ;
explain      = "EXPLAIN" statement ;
```

Expression parsing uses precedence climbing. Precedence, loosest to tightest:
`OR` < `AND` < `NOT` < comparison (`= != < <= > >= IS IN LIKE BETWEEN`) < `+ -` < `* / %` < unary
`- +` < postfix/primary.

---

## 10. Catalog

Three system tables, stored as ordinary heap tables with hardcoded ids, bootstrapped on first open:

```
nanosql_tables  (table_id INTEGER, name TEXT, first_page INTEGER, tuple_count INTEGER)
nanosql_columns (table_id INTEGER, ordinal INTEGER, name TEXT, type INTEGER,
                 nullable BOOLEAN, is_primary BOOLEAN)
nanosql_indexes (index_id INTEGER, table_id INTEGER, name TEXT, root_page INTEGER,
                 is_unique BOOLEAN, column_ordinals TEXT)
```

Because they are ordinary tables, DDL is ordinary DML: it is transactional, logged, and rolled back
for free. The in-memory `Schema` cache is invalidated at DDL commit, under a catalog-wide mutex that
sits *above* everything in §7.1's latch order (take it before any page latch).

---

## 11. Concurrency model of the public API

- A `Database` is thread-safe and shared.
- A `Connection` is **not** thread-safe. One connection per thread. This is documented on the class
  and enforced with a debug-build owning-thread check.
- Many connections may read and write concurrently. Readers never block.
- A `PreparedStatement` belongs to its `Connection` and inherits that restriction.
- A `ResultSet` is a streaming cursor and holds a transaction open until it is closed or exhausted.

---

## 12. Public API — frozen at M0

`include/nanosql/db.h`. `cli`, `api`, and the test harnesses all code against this, so it must exist
before any of them do, even as a header with unimplemented stubs.

```cpp
namespace nanosql {

struct Options {
  size_t buffer_pool_pages   = 4096;    // 16 MiB at 4 KiB pages
  size_t sort_memory_bytes   = 64 << 20;
  bool   create_if_missing   = true;
  bool   sync_on_commit      = true;    // false only for benchmarks; documented as unsafe
};

class Database {
 public:
  static StatusOr<std::unique_ptr<Database>> Open(std::string_view path, const Options& = {});
  StatusOr<std::unique_ptr<Connection>> Connect();
  Status Checkpoint();
  Status Close();
  ~Database();
};

class Connection {
 public:
  Status                            Execute(std::string_view sql);   // no result rows
  StatusOr<ResultSet>               Query(std::string_view sql);
  StatusOr<PreparedStatement>       Prepare(std::string_view sql);
  StatusOr<Transaction>             Begin();
};

class Transaction {
 public:
  Status Commit();
  Status Rollback();
  ~Transaction();            // rolls back if neither was called
};

class PreparedStatement {
 public:
  Status              Bind(int index, const Value& v);
  StatusOr<ResultSet> Execute();
  Status              Reset();
};

class ResultSet {
 public:
  const Schema&     schema() const;
  StatusOr<bool>    Next();                  // false when exhausted
  const Value&      Get(int column) const;
  const Value&      Get(std::string_view column_name) const;
  Status            Close();
};

}  // namespace nanosql
```

Every method returns `Status` or `StatusOr`. Nothing throws. Nothing returns a raw owning pointer.
