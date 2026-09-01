// ARCHITECTURE.md §8: asserts are for logic bugs, Status is for the environment.
//
// NANOSQL_ASSERT is active in *every* build, release included. A database that
// has detected a violated internal invariant cannot safely keep writing; the
// cheapest correct thing it can do is stop before it corrupts the file.
//
// NANOSQL_PARANOID_ASSERT wraps the expensive whole-structure checks (B+Tree
// Validate(), heap space accounting) and compiles to nothing unless
// NANOSQL_PARANOID is defined.
#ifndef NANOSQL_COMMON_ASSERT_H_
#define NANOSQL_COMMON_ASSERT_H_

namespace nanosql::internal {

// Prints file:line, the failed expression, and the message to stderr, then
// aborts. Marked noreturn so callers' control-flow analysis stays accurate.
[[noreturn]] void AssertionFailed(const char* file, int line, const char* func,
                                  const char* expr, const char* message);

}  // namespace nanosql::internal

#define NANOSQL_ASSERT(expr, message)                                                 \
  do {                                                                                \
    if (!(expr)) [[unlikely]] {                                                       \
      ::nanosql::internal::AssertionFailed(__FILE__, __LINE__, __func__, #expr,       \
                                           (message));                                \
    }                                                                                 \
  } while (0)

#define NANOSQL_UNREACHABLE(message) \
  ::nanosql::internal::AssertionFailed(__FILE__, __LINE__, __func__, "unreachable", (message))

#ifdef NANOSQL_PARANOID
#define NANOSQL_PARANOID_ASSERT(expr, message) NANOSQL_ASSERT(expr, message)
#else
// The expression must still parse and typecheck, so it cannot rot while
// paranoid builds are switched off. `sizeof` gives that without evaluating it.
#define NANOSQL_PARANOID_ASSERT(expr, message) \
  do {                                         \
    (void)sizeof((expr) ? 1 : 0);              \
    (void)sizeof(message);                     \
  } while (0)
#endif

#endif  // NANOSQL_COMMON_ASSERT_H_
