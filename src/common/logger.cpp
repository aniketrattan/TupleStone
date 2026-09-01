#include "common/logger.h"

#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace nanosql {
namespace {

std::string_view BaseName(const char* path) {
  std::string_view sv(path);
  const size_t slash = sv.find_last_of("/\\");
  return slash == std::string_view::npos ? sv : sv.substr(slash + 1);
}

}  // namespace

const char* LogLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kTrace: return "TRACE";
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo: return "INFO";
    case LogLevel::kWarn: return "WARN";
    case LogLevel::kError: return "ERROR";
    case LogLevel::kOff: return "OFF";
  }
  return "?";
}

bool ParseLogLevel(std::string_view text, LogLevel* out) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const char c : text) {
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (lowered == "trace") { *out = LogLevel::kTrace; return true; }
  if (lowered == "debug") { *out = LogLevel::kDebug; return true; }
  if (lowered == "info") { *out = LogLevel::kInfo; return true; }
  if (lowered == "warn" || lowered == "warning") { *out = LogLevel::kWarn; return true; }
  if (lowered == "error") { *out = LogLevel::kError; return true; }
  if (lowered == "off" || lowered == "none") { *out = LogLevel::kOff; return true; }
  return false;
}

// Held by a raw pointer and never freed. A logger that is destroyed at static
// destruction time is a logger that cannot report anything going wrong during
// static destruction, which is precisely when the reports would be interesting.
class Logger::State {
 public:
  std::atomic<LogLevel> level{LogLevel::kInfo};
  std::mutex mu;
  std::FILE* stream = stderr;  // guarded by mu
};

Logger::Logger() : state_(new State()) {
  // NANOSQL_LOG_LEVEL lets a test or a bug report raise verbosity without a rebuild.
  if (const char* env = std::getenv("NANOSQL_LOG_LEVEL"); env != nullptr) {
    LogLevel parsed = LogLevel::kInfo;
    if (ParseLogLevel(env, &parsed)) state_->level.store(parsed, std::memory_order_relaxed);
  }
}

Logger& Logger::Global() {
  // Function-local static: initialized exactly once, thread-safely, on first use.
  static Logger* instance = new Logger();
  return *instance;
}

LogLevel Logger::level() const { return state_->level.load(std::memory_order_relaxed); }

void Logger::SetLevel(LogLevel level) { state_->level.store(level, std::memory_order_relaxed); }

void Logger::SetStream(std::FILE* stream) {
  std::lock_guard<std::mutex> lock(state_->mu);
  state_->stream = stream;
}

void Logger::Log(LogLevel level, const char* file, int line, std::string_view message) {
  const std::string_view base = BaseName(file);
  std::lock_guard<std::mutex> lock(state_->mu);
  if (state_->stream == nullptr) return;
  // One fprintf per record: the mutex serializes callers, and a single call
  // keeps a record from being split across a flush.
  std::fprintf(state_->stream, "[%-5s] %.*s:%d: %.*s\n", LogLevelName(level),
               static_cast<int>(base.size()), base.data(), line,
               static_cast<int>(message.size()), message.data());
}

std::string StringPrintf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list args_copy;
  va_copy(args_copy, args);
  const int needed = std::vsnprintf(nullptr, 0, format, args_copy);
  va_end(args_copy);

  std::string result;
  if (needed > 0) {
    result.resize(static_cast<size_t>(needed));
    // C++11 onward guarantees contiguous storage and a writable data(); the
    // trailing NUL vsnprintf writes lands on the string's own terminator.
    std::vsnprintf(result.data(), static_cast<size_t>(needed) + 1, format, args);
  }
  va_end(args);
  return result;
}

}  // namespace nanosql
