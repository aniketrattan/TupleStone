#include "common/logger.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace nanosql {
namespace {

// Captures the global logger's output to an in-memory buffer for the duration
// of a test, and restores the level and stream afterwards.
class CaptureLog {
 public:
  explicit CaptureLog(LogLevel level) : saved_level_(Logger::Global().level()) {
    file_ = std::tmpfile();
    Logger::Global().SetStream(file_);
    Logger::Global().SetLevel(level);
  }

  ~CaptureLog() {
    Logger::Global().SetStream(stderr);
    Logger::Global().SetLevel(saved_level_);
    if (file_ != nullptr) std::fclose(file_);
  }

  CaptureLog(const CaptureLog&) = delete;
  CaptureLog& operator=(const CaptureLog&) = delete;

  std::string Text() {
    std::fflush(file_);
    std::rewind(file_);
    std::string out;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), file_)) > 0) out.append(buf, n);
    return out;
  }

 private:
  LogLevel saved_level_;
  std::FILE* file_ = nullptr;
};

TEST(LoggerTest, LevelNamesRoundTripThroughTheParser) {
  const std::vector<LogLevel> levels = {LogLevel::kTrace, LogLevel::kDebug, LogLevel::kInfo,
                                        LogLevel::kWarn, LogLevel::kError, LogLevel::kOff};
  for (const LogLevel level : levels) {
    LogLevel parsed = LogLevel::kInfo;
    ASSERT_TRUE(ParseLogLevel(LogLevelName(level), &parsed)) << LogLevelName(level);
    EXPECT_EQ(parsed, level);
  }
}

TEST(LoggerTest, ParseIsCaseInsensitiveAndRejectsGarbage) {
  LogLevel parsed = LogLevel::kInfo;
  EXPECT_TRUE(ParseLogLevel("WARN", &parsed));
  EXPECT_EQ(parsed, LogLevel::kWarn);
  EXPECT_TRUE(ParseLogLevel("Warning", &parsed));
  EXPECT_EQ(parsed, LogLevel::kWarn);

  parsed = LogLevel::kError;
  EXPECT_FALSE(ParseLogLevel("verbose", &parsed));
  EXPECT_EQ(parsed, LogLevel::kError) << "a failed parse must leave the output untouched";
}

TEST(LoggerTest, WritesLevelFileLineAndMessage) {
  CaptureLog capture(LogLevel::kTrace);
  NANOSQL_LOG_WARN("disk %s after %d retries", "gave up", 3);
  const std::string text = capture.Text();
  EXPECT_NE(text.find("WARN"), std::string::npos) << text;
  EXPECT_NE(text.find("logger_test.cpp"), std::string::npos) << text;
  EXPECT_NE(text.find("disk gave up after 3 retries"), std::string::npos) << text;
}

TEST(LoggerTest, SuppressesRecordsBelowTheLevel) {
  CaptureLog capture(LogLevel::kWarn);
  NANOSQL_LOG_DEBUG("this must not appear");
  NANOSQL_LOG_INFO("nor this");
  NANOSQL_LOG_ERROR("but this must");
  const std::string text = capture.Text();
  EXPECT_EQ(text.find("must not appear"), std::string::npos) << text;
  EXPECT_EQ(text.find("nor this"), std::string::npos) << text;
  EXPECT_NE(text.find("but this must"), std::string::npos) << text;
}

TEST(LoggerTest, OffSuppressesEverything) {
  CaptureLog capture(LogLevel::kOff);
  NANOSQL_LOG_ERROR("silence");
  EXPECT_TRUE(capture.Text().empty());
}

// A disabled log statement must not evaluate its arguments, or a trace call in a
// hot path would cost whatever its arguments cost even when tracing is off.
TEST(LoggerTest, DisabledRecordDoesNotEvaluateArguments) {
  CaptureLog capture(LogLevel::kError);
  int side_effects = 0;
  const auto counted = [&side_effects]() {
    ++side_effects;
    return 1;
  };
  NANOSQL_LOG_DEBUG("%d", counted());
  EXPECT_EQ(side_effects, 0);
  NANOSQL_LOG_ERROR("%d", counted());
  EXPECT_EQ(side_effects, 1);
}

TEST(LoggerTest, ConcurrentRecordsAreNotInterleaved) {
  CaptureLog capture(LogLevel::kInfo);
  constexpr int kThreads = 8;
  constexpr int kPerThread = 200;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([t]() {
      for (int i = 0; i < kPerThread; ++i) {
        NANOSQL_LOG_INFO("thread=%d seq=%d payload=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", t, i);
      }
    });
  }
  for (std::thread& thread : threads) thread.join();

  const std::string text = capture.Text();
  int complete_lines = 0;
  size_t start = 0;
  while (start < text.size()) {
    const size_t nl = text.find('\n', start);
    if (nl == std::string::npos) break;
    const std::string line = text.substr(start, nl - start);
    // Every record ends with the fixed payload; a torn line would not.
    EXPECT_NE(line.find("payload=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), std::string::npos)
        << "torn line: " << line;
    ++complete_lines;
    start = nl + 1;
  }
  EXPECT_EQ(complete_lines, kThreads * kPerThread);
}

TEST(StringPrintfTest, FormatsWithoutTruncation) {
  EXPECT_EQ(StringPrintf("%s=%d", "n", 42), "n=42");
  EXPECT_EQ(StringPrintf("no args"), "no args");
  EXPECT_EQ(StringPrintf("%s", ""), "");

  const std::string long_arg(10000, 'x');
  const std::string result = StringPrintf("[%s]", long_arg.c_str());
  EXPECT_EQ(result.size(), long_arg.size() + 2);
  EXPECT_EQ(result.front(), '[');
  EXPECT_EQ(result.back(), ']');
}

}  // namespace
}  // namespace nanosql
