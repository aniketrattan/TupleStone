# nanosql

An embedded, ACID, MVCC relational database with a real SQL front end, written from scratch in
C++20 with **no third-party runtime dependencies**.

Shaped like SQLite — one file per database, linked into your process — but with Postgres-style
multi-version concurrency control: heap-stored row versions, snapshot isolation, and readers that
never block writers.

> **Status: M0 of 13.** The repository builds, tests, and lints itself, the shared `common`
> primitives are in place, and the public API surface is frozen. No storage engine yet — see
> [PLAN.md](PLAN.md) for the roadmap and [docs/design/](docs/design/) for what has actually landed.

## Design

Three documents, in reading order:

| Document | What it is |
|---|---|
| [PLAN.md](PLAN.md) | The roadmap: thirteen milestones, what each builds, and its exit criterion |
| [ARCHITECTURE.md](ARCHITECTURE.md) | The frozen design: file formats, protocols, invariants, SQL grammar |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Rules of engagement for anyone (or anything) writing code here |

The layers depend strictly downward, and the CMake link graph is what enforces it:

```
cli → api → exec → planner → sql → catalog → txn → {index, table} → wal → buffer → disk → common
```

## Building

Requires CMake ≥ 3.25, a C++20 compiler, and Ninja. GoogleTest is fetched at configure time and is
the only dependency — test-only, as is Google Benchmark.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

### Presets

| Preset | Purpose |
|---|---|
| `debug` | `-O0 -g`, `NANOSQL_PARANOID` on (the expensive invariant checks run) |
| `release` | `RelWithDebInfo`, benchmarks enabled |
| `asan` | AddressSanitizer |
| `ubsan` | UndefinedBehaviorSanitizer, `-fno-sanitize-recover=all` |
| `tsan` | ThreadSanitizer — required green for anything touching concurrency |
| `mingw-debug` / `mingw-release` | Same as `debug`/`release` but with the MinGW Makefiles generator, for Windows setups without Ninja |

**On Windows/MinGW the three sanitizer presets do not configure**, and say so with a clear message:
the MinGW GCC toolchain ships no sanitizer runtimes. Run them on Linux, where CI does.

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
