# M0 — Skeleton (historical baseline)

**Status:** complete. Exit criterion met on the two presets this machine can run; see
[Known gaps](#known-gaps) for the three it cannot.

## What was built

This note records the original repository baseline. The later vertical-slice implementation now
extends it with storage, SQL, and API layers; the M0 observations remain useful for the build
toolchain but are no longer a statement that the engine is stubbed.

| Area | Files |
|---|---|
| Build | `CMakeLists.txt`, `CMakePresets.json`, `src/*/CMakeLists.txt`, `tests/*/CMakeLists.txt` |
| Lint / VCS | `.clang-format`, `.clang-tidy`, `.gitignore`, `.github/workflows/ci.yml` |
| Public API | `include/tuplestone/{status.h,value.h,db.h}`, implemented in `src/api/db.cpp` |
| Primitives | `src/common/{types.h,slice.h,endian.h,assert.h,crc32c.*,logger.*,status.cpp}` |
| Tests | `tests/unit/` common, API, engine, and storage coverage — 72 cases |

### Layer targets

Each `src/<layer>/` is one CMake target, and `common` links nothing but the standard library. The
link graph is the enforcement mechanism for ARCHITECTURE.md §1's rule that dependencies point
strictly downward — an upward include will fail to link rather than merely offending a reviewer.

`tuplestone_options` is an INTERFACE target carrying the warning set, the sanitizer flags, and the
`TUPLESTONE_PARANOID` / `TUPLESTONE_VERSION` definitions. Every tuplestone target links it privately;
fetched dependencies do not, so `-Werror` applies to this project's code and not to GoogleTest's.

## Non-obvious choices

**`Status` is one pointer wide when Ok.** The message and source position live in a heap `Payload`
allocated only on failure. Since the overwhelmingly common case is success — every page read, every
comparison — the success path costs a null check and no allocation. The cost is that copying a
failed `Status` allocates; failures are rare enough that this is the right trade.

**Constructing an Ok `Status` with a message silently drops the message.** The alternative states
are worse: an object whose `ok()` disagrees with its contents, or an assert on a call that is
merely pointless rather than wrong.

**`StatusOr<T>` value-initializes its value for an Ok `Status`.** Mutation entry points share a
`StatusOr` return type with queries, so a successful mutation carries an empty result object.

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

**The public API is frozen and now implemented by the compact vertical slice.** The original M0
surface remains source-compatible; invalid default handles return a status and stable empty/default
accessor values.
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
toolchain (CMake 4.4.2, g++ 14.2.0, MSYS2 UCRT64) used by the current build and CI configuration.

1. **The `asan`, `ubsan`, and `tsan` presets have never been run.** MinGW GCC ships no sanitizer
   runtimes — `-fsanitize=address|undefined|thread` all fail to link. `CMakeLists.txt` detects this
   and fails configuration with an explanatory message rather than an inscrutable link error. CI
   runs all three on Linux under both GCC and Clang, so they are covered on every push; they are
   simply not covered *locally*. **The first person to run CI should confirm they are green before
   trusting M0 as done.**

2. **Local tool provenance is explicit.** Ninja is available in the user Python tools directory,
   while the verified C++ compiler remains MSYS2 UCRT64 GCC 14.2.0. CI installs its own Ninja and
   does not depend on the developer machine's PATH.

3. **Formatting is checked, not assumed.** The C++ tooling's LLVM `clang-format` was applied to
   the repository and a local `--dry-run --Werror` over every tracked C++ source/header passes. CI
   repeats the same check on Ubuntu.

## Verification

```
ctest --test-dir build/debug    72/72 passed
ctest --test-dir build/release  72/72 passed
clang-format --dry-run --Werror (all tracked C++ files) passed
```

The implementation sources were directly compiled and linked under `-Wall -Wextra -Wpedantic -Werror`
plus `-Wshadow`, `-Wconversion`, `-Wsign-conversion`, `-Wold-style-cast`, and `-Wformat=2`; the CMake
presets configure cleanly. In this managed Windows shell, the Python-distributed Ninja process can
hang while spawning its first compiler, so the local evidence uses the equivalent direct compiler
invocations and CTest binaries. CI remains the clean-clone CMake build authority.

## Next

The next portfolio-depth tranche is page-backed heap/index integration and the full ARIES recovery
path. The current checkpoint is intentionally useful before that work: it demonstrates SQL, a
durable snapshot WAL, restart replay, a CLI, and reproducible test/benchmark evidence without
claiming features that are not wired through the public path yet.
