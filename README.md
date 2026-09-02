# TupleStone — a C++20 embedded SQL engine

A C++20 embedded SQL database prototype with a durable commit log, typed values, and a small
relational front end. The implementation is intentionally being built from scratch with **no
third-party runtime dependencies**.

Shaped like SQLite — one file per database, linked into your process. The roadmap targets
Postgres-style MVCC; the current vertical slice uses copy-on-write snapshots and first-committer-
wins generation checks while tuple-version storage is built out.

> **Status:** TupleStone is a working compact vertical slice. The current release name is reflected
> consistently in the namespace, library, CLI, on-disk markers, and documentation.

The current implementation includes checksummed page I/O and free-page reuse, a bounded buffer
pool, heap/index/WAL/transaction primitives, a durable snapshot commit log with restart replay,
typed values, a rule-based SQL evaluator (`CREATE/DROP TABLE`, `INSERT`, `SELECT`, `UPDATE`,
`DELETE`, transactions, prepared parameters), and a small `tuplestone` shell. It is designed as a
usable vertical slice while the remaining production-hardening work in the roadmap (page-backed
heap/index integration, full ARIES recovery, join/aggregate operators, and sanitizer/fuzz suites)
is completed.

Index DDL is intentionally rejected with `NotSupported` until the public path can maintain
page-backed indexes correctly; a successful no-op would be worse than an explicit boundary.

## Design

Three documents, in reading order:

| Document | What it is |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | The frozen design: file formats, protocols, invariants, SQL grammar |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Rules of engagement for contributors |
| [docs/PORTFOLIO.md](docs/PORTFOLIO.md) | Demonstration scope, current guarantees, and known limitations |

The layers depend strictly downward, and the CMake link graph is what enforces it:

```
cli → api → exec → planner → sql → catalog → txn → {index, table} → wal → buffer → disk → common
```

## Building

Requires CMake ≥ 3.25, a C++20 compiler, and Ninja. GoogleTest is fetched at configure time and is
the only third-party dependency; the release benchmark is dependency-free.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

### Presets

| Preset | Purpose |
|---|---|
| `debug` | `-O0 -g`, `TUPLESTONE_PARANOID` on (the expensive invariant checks run) |
| `release` | `RelWithDebInfo`, benchmarks enabled |
| `asan` | AddressSanitizer |
| `ubsan` | UndefinedBehaviorSanitizer, `-fno-sanitize-recover=all` |
| `tsan` | ThreadSanitizer — required green for anything touching concurrency |
| `mingw-debug` / `mingw-release` | Same as `debug`/`release` but with the MinGW Makefiles generator, for Windows setups without Ninja |

**On Windows/MinGW the three sanitizer presets do not configure**, and say so with a clear message:
the MinGW GCC toolchain ships no sanitizer runtimes. Run them on Linux, where CI does.

The release preset also builds `tuplestone_bench`, a dependency-free API benchmark. Run it with
`build\\release\\bin\\tuplestone_bench.exe [database-path] [row-count]`.

## Testing

Rigor is a requirement of this project, not a nice-to-have.

- **Unit tests** — GoogleTest, `tests/unit/`, shipped in the same commit as the code they cover.
- **SQL logic tests** — `tests/slt/`, sqllogictest-style; the primary regression net from M11.
- **Crash injection** — a fault-injecting disk manager plus a subprocess harness; a fixed corpus of
  crash points in CI, randomized seeds in a local soak target.
- **Fuzzing** — libFuzzer targets with a permanently checked-in corpus.
- **Sanitizers** — ASan and UBSan on every CI run; TSan on the concurrency subset. A sanitizer report
  is a build failure, never a known flake.

Every RNG is explicitly seeded and prints its seed on failure.

## Deliberately out of scope

No networking or server daemon. No replication or sharding. No triggers, views, or stored procedures.
No window functions or CTEs. No users or permissions. No join reordering.

And one that is a *consequence* rather than an omission: the isolation level is snapshot isolation
and only that, so **write skew is possible**. Two transactions can each read a set, each update a
different row in it, and both commit. That is snapshot isolation behaving as specified.
Serializable Snapshot Isolation is future work.

## License

Not yet chosen.
