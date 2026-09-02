// A minimal, dependency-free logger. Deliberately not configurable beyond a
// level and a stream: logging is a debugging aid here, not a product feature,
// and anything richer would be the first third-party dependency.
//
// Thread-safe: each record is formatted into a local buffer and written under a
// mutex, so lines from concurrent threads never interleave.
#ifndef TUPLESTONE_COMMON_LOGGER_H_
#define TUPLESTONE_COMMON_LOGGER_H_

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace tuplestone {

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

  // Callers should go through the TUPLESTONE_LOG_* macros, which skip formatting
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
#define TUPLESTONE_PRINTF_FORMAT(fmt_index, first_arg) \
  __attribute__((format(gnu_printf, fmt_index, first_arg)))
#elif defined(__GNUC__)
#define TUPLESTONE_PRINTF_FORMAT(fmt_index, first_arg) \
  __attribute__((format(printf, fmt_index, first_arg)))
#else
#define TUPLESTONE_PRINTF_FORMAT(fmt_index, first_arg)
#endif

std::string StringPrintf(const char* format, ...) TUPLESTONE_PRINTF_FORMAT(1, 2);

}  // namespace tuplestone

// The level test happens before the arguments are evaluated, so a disabled log
// statement costs one comparison and never formats anything.
#define TUPLESTONE_LOG(level_enum, ...)                                                           \
  do {                                                                                         \
    ::tuplestone::Logger& _tuplestone_lg = ::tuplestone::Logger::Global();                              \
    if (_tuplestone_lg.Enabled(level_enum)) {                                                     \
      _tuplestone_lg.Log((level_enum), __FILE__, __LINE__, ::tuplestone::StringPrintf(__VA_ARGS__)); \
    }                                                                                          \
  } while (0)

#define TUPLESTONE_LOG_TRACE(...) TUPLESTONE_LOG(::tuplestone::LogLevel::kTrace, __VA_ARGS__)
#define TUPLESTONE_LOG_DEBUG(...) TUPLESTONE_LOG(::tuplestone::LogLevel::kDebug, __VA_ARGS__)
#define TUPLESTONE_LOG_INFO(...) TUPLESTONE_LOG(::tuplestone::LogLevel::kInfo, __VA_ARGS__)
#define TUPLESTONE_LOG_WARN(...) TUPLESTONE_LOG(::tuplestone::LogLevel::kWarn, __VA_ARGS__)
#define TUPLESTONE_LOG_ERROR(...) TUPLESTONE_LOG(::tuplestone::LogLevel::kError, __VA_ARGS__)

#endif  // TUPLESTONE_COMMON_LOGGER_H_
