# CONTRIBUTING.md — rules for agents working on TupleStone

Read this before writing any code in this repository.

## Orientation

1. Read [PLAN.md](PLAN.md) — what is being built and in what order.
2. Read [ARCHITECTURE.md](ARCHITECTURE.md) — the frozen design: file formats, protocols, invariants,
   SQL grammar, public API.
3. Check `docs/design/` to see which milestones are already done. The highest-numbered note there is
   the last completed milestone.

**Never skip ahead.** If M5 is not done, do not start M6, even if M6 looks more interesting. The
dependency order in PLAN.md is real: skipped foundations resurface as unexplainable corruption two
milestones later. Chaining several milestones in one sitting is fine — finishing each one against
the checklist below before starting the next is not optional.

## Definition of done

A milestone is not done when the feature works. It is done when **all** of these hold:

- [ ] Implementation complete, matching the formats and protocols in ARCHITECTURE.md exactly.
- [ ] Unit tests in `tests/unit/<module>_test.cpp`, written in the same commit as the code.
- [ ] The milestone's **exit criterion** from PLAN.md passes — that specific test, not a substitute.
- [ ] `ctest` green under the `debug`, `release`, `asan`, and `ubsan` presets.
- [ ] `tsan` preset green for anything touching concurrency (buffer pool, B+Tree, txn manager, WAL).
- [ ] All previously passing tests still pass, including the `.slt` suite once it exists.
- [ ] `clang-format` clean.
- [ ] A design note at `docs/design/NN-<name>.md`: what was built, why any non-obvious choice was
      made, what was learned, and what is known to be incomplete.

Report honestly against this list. "Implemented, tests written, but the TSan run has one report I
haven't diagnosed" is a useful status. "Done" when TSan is red is not.

## Code rules

- **C++20.** No compiler extensions.
- **No third-party runtime dependencies.** GoogleTest is test-only and pulled via `FetchContent`; the
  current benchmark smoke target is dependency-free. Nothing else gets added without an ADR — writing
  it yourself *is* the project.
- **RAII for every resource.** No raw `new`/`delete`. No manual `close()`/`unlatch()` paths that an
  early return can skip.
- **No naked `Page*` outside `src/buffer/`.** Always a `PageGuard`. A `PageGuard` never outlives the
  function that created it and is never stored in a member.
- **No exceptions across the public API.** `Status` / `StatusOr<T>` for recoverable failures.
- **Asserts are for logic bugs, `Status` is for the environment.** See ARCHITECTURE.md §8.
- **Never `memcpy` a struct to disk.** All serialization goes through explicit little-endian
  field-by-field helpers in `common`.
- **Latch order is non-negotiable.** ARCHITECTURE.md §7.1. If a change seems to require violating it,
  the change is wrong — restructure so it doesn't.

### Naming and formatting

| Thing | Style |
|---|---|
| Types | `PascalCase` — `BufferPoolManager`, `TableHeap` |
| Functions, methods | `PascalCase` — `FetchPage`, `InsertTuple` |
| Variables, parameters | `snake_case` — `page_id`, `tuple_count` |
| Private members | trailing underscore — `page_table_`, `pool_size_` |
| Constants | `kPascalCase` — `kPageSize`, `kInvalidPageId` |
| Files | `snake_case.cpp` / `.h` |
| Macros | `TUPLESTONE_SCREAMING` |

`.clang-format` is authoritative and checked in CI. Do not hand-format around it.

Comments explain **why**, not what. A comment restating the code is noise; a comment explaining why
the latch is released before the log append is essential.

## Changing the architecture

ARCHITECTURE.md is frozen. If implementation reveals that a decision in it is wrong — and this will
happen at least once, most likely in M6 or M7 — then:

1. **Stop and ask the human.** Do not silently deviate and do not silently work around it.
2. If the change is approved, write `docs/adr/NNNN-<title>.md`: context, the decision, the
   alternatives considered, and the consequences.
3. Update ARCHITECTURE.md to the new decision, and link the ADR from it.

An implementation that quietly disagrees with ARCHITECTURE.md is worse than either option, because
the next agent will trust the document.

## Testing rules

- **Never weaken, skip, or delete a test to make a build green.** If a test fails, either the code is
  wrong or the test's expectation is wrong — determine which, and say which in the commit message.
- **Never leave a `TODO` in place of a correctness check** on the durability path (WAL ordering,
  fsync, recovery) or the visibility path (MVCC snapshot rules). A `TODO` anywhere else is fine if
  it's tracked in the milestone's design note.
- **A TSan or ASan report is a build failure**, never a "known flake". Race reports in a database are
  future corruption reports.
- Tests must be **deterministic**. Seed every RNG explicitly and print the seed on failure. Concurrency
  tests use a deterministic scheduler or barriers, not `sleep`.
- Any input that ever crashed a fuzz target goes into `tests/fuzz/corpus/` permanently.

## Commits

- One logical step per commit; do not batch a whole milestone into one.
- Format: `<layer>: <imperative summary>`
  - `btree: split internal nodes on overflow`
  - `wal: flush log through page_lsn before page write`
  - `docs: add design note for M5`
- The body explains why, and names any test added.
- Do not commit build artifacts, `.vscode/`, or benchmark scratch output.

## Working style

- When something is ambiguous between PLAN.md and ARCHITECTURE.md, ARCHITECTURE.md wins on *how* and
  PLAN.md wins on *what* and *when*.
- Prefer boring, readable code over clever code. This project is read by humans as a portfolio piece;
  a clear B+Tree beats a fast unreadable one.
- Before adding a helper, grep `src/common/` — the primitive you want probably exists.
- If a milestone turns out to be larger than expected, finish it properly and say so, rather than
  shipping half of it as done.
