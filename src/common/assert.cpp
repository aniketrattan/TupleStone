#include "common/assert.h"

#include <cstdio>
#include <cstdlib>

namespace nanosql::internal {

void AssertionFailed(const char* file, int line, const char* func, const char* expr,
                     const char* message) {
  std::fflush(stdout);
  std::fprintf(stderr,
               "\nnanosql: assertion failed\n"
               "  at       %s:%d\n"
               "  in       %s\n"
               "  expected %s\n"
               "  message  %s\n",
               file, line, func, expr, message != nullptr ? message : "(none)");
  std::fflush(stderr);
  // std::abort, not exit: a core dump is the point of reaching this line.
  std::abort();
}

}  // namespace nanosql::internal
