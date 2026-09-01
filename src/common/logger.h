// A minimal, dependency-free logger. Deliberately not configurable beyond a
// level and a stream: logging is a debugging aid here, not a product feature,
// and anything richer would be the first third-party dependency.
//
// Thread-safe: each record is formatted into a local buffer and written under a
// mutex, so lines from concurrent threads never interleave.
#ifndef NANOSQL_COMMON_LOGGER_H_
#define NANOSQL_COMMON_LOGGER_H_

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace nanosql {

enum class LogLevel : uint8_t {
  kTrace = 0,
  kDebug,
  kInfo,
  kWarn,
  kError,
  kOff,
};

const char* LogLevelName(LogLevel level);

// Parses "trace"/"debug"/"info"/"warn"/"error"/"off", case-insensitively.
// Returns false and leaves *out untouched if the text is not a level name.
bool ParseLogLevel(std::string_view text, LogLevel* out);

class Logger {
 public:
  static Logger& Global();

  LogLevel level() const;
  void SetLevel(LogLevel level);

  // The logger does not take ownership and does not close the stream. Passing
  // nullptr silences output without changing the level.
  void SetStream(std::FILE* stream);

  bool Enabled(LogLevel level) const { return level >= this->level(); }

  // Callers should go through the NANOSQL_LOG_* macros, which skip formatting
  // entirely when the level is disabled.
  void Log(LogLevel level, const char* file, int line, std::string_view message);

 private:
  Logger();

  class State;
  State* state_;  // leaked-on-purpose singleton state; see logger.cpp
};

// Formats with printf semantics. Checked by the compiler under GCC and Clang.
// gnu_printf, not printf: on MinGW the bare printf archetype means the old
// msvcrt dialect, which rejects %zu and friends even though the UCRT accepts them.
#if defined(__MINGW32__)
#define NANOSQL_PRINTF_FORMAT(fmt_index, first_arg) \
  __attribute__((format(gnu_printf, fmt_index, first_arg)))
#elif defined(__GNUC__)
#define NANOSQL_PRINTF_FORMAT(fmt_index, first_arg) \
  __attribute__((format(printf, fmt_index, first_arg)))
#else
#define NANOSQL_PRINTF_FORMAT(fmt_index, first_arg)
#endif

std::string StringPrintf(const char* format, ...) NANOSQL_PRINTF_FORMAT(1, 2);

}  // namespace nanosql

// The level test happens before the arguments are evaluated, so a disabled log
// statement costs one comparison and never formats anything.
#define NANOSQL_LOG(level_enum, ...)                                                 \
  do {                                                                               \
    ::nanosql::Logger& _nanosql_lg = ::nanosql::Logger::Global();                    \
    if (_nanosql_lg.Enabled(level_enum)) {                                           \
      _nanosql_lg.Log((level_enum), __FILE__, __LINE__,                              \
                      ::nanosql::StringPrintf(__VA_ARGS__));                         \
    }                                                                                \
  } while (0)

#define NANOSQL_LOG_TRACE(...) NANOSQL_LOG(::nanosql::LogLevel::kTrace, __VA_ARGS__)
#define NANOSQL_LOG_DEBUG(...) NANOSQL_LOG(::nanosql::LogLevel::kDebug, __VA_ARGS__)
#define NANOSQL_LOG_INFO(...) NANOSQL_LOG(::nanosql::LogLevel::kInfo, __VA_ARGS__)
#define NANOSQL_LOG_WARN(...) NANOSQL_LOG(::nanosql::LogLevel::kWarn, __VA_ARGS__)
#define NANOSQL_LOG_ERROR(...) NANOSQL_LOG(::nanosql::LogLevel::kError, __VA_ARGS__)

#endif  // NANOSQL_COMMON_LOGGER_H_
