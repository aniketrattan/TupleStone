# M0 — Skeleton

**Status:** complete. Exit criterion met on the two presets this machine can run; see
[Known gaps](#known-gaps) for the three it cannot.

## What was built

A repository that builds, tests, and lints itself, plus the `common` primitives every later layer
depends on.

| Area | Files |
|---|---|
| Build | `CMakeLists.txt`, `CMakePresets.json`, `src/*/CMakeLists.txt`, `tests/*/CMakeLists.txt` |
| Lint / VCS | `.clang-format`, `.clang-tidy`, `.gitignore`, `.github/workflows/ci.yml` |
| Public API | `include/nanosql/{status.h,value.h,db.h}`, stubbed in `src/api/db.cpp` |
| Primitives | `src/common/{types.h,slice.h,endian.h,assert.h,crc32c.*,logger.*,status.cpp}` |
| Tests | `tests/unit/{status,slice,endian,crc32c,logger,types,api_surface}_test.cpp` — 65 cases |

### Layer targets

Each `src/<layer>/` is one CMake target, and `common` links nothing but the standard library. The
link graph is the enforcement mechanism for ARCHITECTURE.md §1's rule that dependencies point
strictly downward — an upward include will fail to link rather than merely offending a reviewer.

`nanosql_options` is an INTERFACE target carrying the warning set, the sanitizer flags, and the
`NANOSQL_PARANOID` / `NANOSQL_VERSION` definitions. Every nanosql target links it privately;
fetched dependencies do not, so `-Werror` applies to this project's code and not to GoogleTest's.

## Non-obvious choices

**`Status` is one pointer wide when Ok.** The message and source position live in a heap `Payload`
allocated only on failure. Since the overwhelmingly common case is success — every page read, every
comparison — the success path costs a null check and no allocation. The cost is that copying a
failed `Status` allocates; failures are rare enough that this is the right trade.

**Constructing an Ok `Status` with a message silently drops the message.** The alternative states
are worse: an object whose `ok()` disagrees with its contents, or an assert on a call that is
merely pointless rather than wrong.

**`StatusOr<T>` aborts if constructed from an Ok `Status`.** That would leave it holding neither a
value nor an error, which nothing downstream can handle correctly. It can only happen through a
call-site bug, so per ARCHITECTURE.md §8 it aborts rather than returning a `Status`.

**Both endiannesses are in `endian.h`, for different jobs.** Little-endian is the file format
(§3). Big-endian exists for exactly one reason — the memcomparable key encoding of §6.2 needs
most-significant-byte-first so that `memcmp` reproduces numeric order — and `endian_test.cpp`
asserts that property directly, on 20k random pairs, so M4 inherits a tested foundation.

**`Slice::Compare` special-cases a zero-length compare.** A default-constructed `Slice` has a null
data pointer, and passing null to `memcmp` is undefined behaviour even when the length is zero.
UBSan would eventually have caught this; catching it now was cheaper.

**CRC32C is a plain byte-at-a-time table, generated `constexpr`.** A slicing-by-8 implementation is
roughly 4× faster and an SSE4.2 `crc32` intrinsic path is far faster still, but neither is needed
until page I/O is on a measured hot path, and CONTRIBUTING.md prefers the readable version. The `constexpr`
table sidesteps any static-initialization-order question about when the table becomes valid.
Correctness is pinned to the standard check vector (`"123456789"` → `0xE3069283`) and the three
RFC 3720 Appendix B vectors, so a future fast rewrite has something exact to be checked against.

**The logger's singleton state is intentionally leaked.** A logger destroyed during static
destruction cannot report anything going wrong during static destruction, which is exactly when
those reports would be interesting.

**The public API is frozen but not implemented.** ARCHITECTURE.md §12 requires the surface to exist
before `cli`, `api`, or the harnesses code against it. Every entry point returns
`Status::NotSupported`; the two accessors that must return a reference (`ResultSet::schema`,
`ResultSet::Get`) cannot express that, so they abort — reaching them before M12 is a caller bug.
`api_surface_test.cpp` pins the signatures: it will not compile if one drifts.

## What was learned

**MinGW's `ld` 2.43.1 cannot link a large C++ program against the shared libstdc++/libgcc DLLs.**
It exits 116 with no diagnostic at all — no undefined symbol, no message, nothing. GoogleTest alone
is enough to trigger it; a trivial `main` is not. Bisecting archive by archive proved the symbols
were all present and defined, which ruled out the obvious explanations and left the runtime link
itself. Passing `-static-libstdc++ -static-libgcc` fixes it, and is applied under `if(MINGW)` in
`CMakeLists.txt`. Side benefit: the test binaries then run without a DLL search path.

**MinGW's `printf` format archetype is the old msvcrt dialect.** `__attribute__((format(printf,…)))`
there rejects `%zu` and other C99 specifiers that the UCRT actually accepts. `logger.h` uses
`gnu_printf` on `__MINGW32__` so the compiler's format checking matches the runtime's behaviour.

**`-Wformat=2` rejects a zero-length format string.** `StringPrintf("")` is a build error under the
warning set, which is a fair complaint; the test asserts `StringPrintf("%s", "")` instead.

## Known gaps

These are honest gaps, not oversights. Each is a real environment constraint on the verified
toolchain (CMake 4.4.2, g++ 14.2.0, MSYS2 UCRT64) named in PLAN.md.

1. **The `asan`, `ubsan`, and `tsan` presets have never been run.** MinGW GCC ships no sanitizer
   runtimes — `-fsanitize=address|undefined|thread` all fail to link. `CMakeLists.txt` detects this
   and fails configuration with an explanatory message rather than an inscrutable link error. CI
   runs all three on Linux under both GCC and Clang, so they are covered on every push; they are
   simply not covered *locally*. **The first person to run CI should confirm they are green before
   trusting M0 as done.**

2. **Ninja is not installed and could not be.** `pacman` resolves
   `mingw-w64-ucrt-x86_64-ninja` into a `gcc-libs` upgrade that would drag GCC from 14.2.0 to
   16.2.0 — off the toolchain PLAN.md names as verified. Rather than silently change the compiler,
   the presets keep Ninja (CI uses it, and it is what PLAN.md specifies) and two additional presets,
   `mingw-debug` and `mingw-release`, use the MinGW Makefiles generator for local work. They differ
   from `debug` / `release` in generator only.

3. **`clang-format` is not installed, so the checked-in `.clang-format` has never been applied.**
   The sources were hand-written to Google style at a 100-column limit, which is what the config
   specifies, but the CI `format` job is the first thing that will actually verify it and it may
   well reformat on the first run. That is expected; do not hand-format around it.

## Verification

```
ctest --preset mingw-debug     65/65 passed
ctest --preset mingw-release   65/65 passed
```

Both configure and build clean under `-Wall -Wextra -Wpedantic -Werror` plus `-Wshadow`,
`-Wconversion`, `-Wsign-conversion`, `-Wold-style-cast`, and `-Wformat=2`.

## Next

M1 — disk manager and pages. It introduces `FaultDiskManager`, the fault-injecting test double that
M6's crash suite is built on. PLAN.md is explicit that it gets built in M1, not deferred to M6.
