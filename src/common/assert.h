// ARCHITECTURE.md §8: asserts are for logic bugs, Status is for the environment.
//
// TUPLESTONE_ASSERT is active in *every* build, release included. A database that
// has detected a violated internal invariant cannot safely keep writing; the
// cheapest correct thing it can do is stop before it corrupts the file.
//
// TUPLESTONE_PARANOID_ASSERT wraps the expensive whole-structure checks (B+Tree
// Validate(), heap space accounting) and compiles to nothing unless
// TUPLESTONE_PARANOID is defined.
#ifndef TUPLESTONE_COMMON_ASSERT_H_
#define TUPLESTONE_COMMON_ASSERT_H_

namespace tuplestone::internal {

// Prints file:line, the failed expression, and the message to stderr, then
// aborts. Marked noreturn so callers' control-flow analysis stays accurate.
[[noreturn]] void AssertionFailed(const char* file, int line, const char* func, const char* expr,
                                  const char* message);

}  // namespace tuplestone::internal

#define TUPLESTONE_ASSERT(expr, message)                                                       \
  do {                                                                                         \
    if (!(expr)) [[unlikely]] {                                                                \
      ::tuplestone::internal::AssertionFailed(__FILE__, __LINE__, __func__, #expr, (message)); \
    }                                                                                          \
  } while (0)

#define TUPLESTONE_UNREACHABLE(message) \
  ::tuplestone::internal::AssertionFailed(__FILE__, __LINE__, __func__, "unreachable", (message))

#ifdef TUPLESTONE_PARANOID
#define TUPLESTONE_PARANOID_ASSERT(expr, message) TUPLESTONE_ASSERT(expr, message)
#else
// The expression must still parse and typecheck, so it cannot rot while
// paranoid builds are switched off. `sizeof` gives that without evaluating it.
#define TUPLESTONE_PARANOID_ASSERT(expr, message) \
  do {                                            \
    (void)sizeof((expr) ? 1 : 0);                 \
    (void)sizeof(message);                        \
  } while (0)
#endif

#endif  // TUPLESTONE_COMMON_ASSERT_H_
