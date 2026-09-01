#include "nanosql/status.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace nanosql {
namespace {

TEST(StatusTest, DefaultIsOk) {
  const Status s;
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kOk);
  EXPECT_TRUE(s.message().empty());
  EXPECT_FALSE(s.pos().valid());
  EXPECT_EQ(s.ToString(), "Ok");
}

TEST(StatusTest, OkFactoryMatchesDefault) { EXPECT_EQ(Status::Ok(), Status()); }

TEST(StatusTest, CarriesCodeAndMessage) {
  const Status s = Corruption("page 7 failed its checksum");
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kCorruption);
  EXPECT_EQ(s.message(), "page 7 failed its checksum");
  EXPECT_EQ(s.ToString(), "Corruption: page 7 failed its checksum");
}

TEST(StatusTest, CarriesSourcePositionForSqlErrors) {
  const Status s = SyntaxError("unexpected token \"FROMM\"", SourcePos{1, 23});
  ASSERT_TRUE(s.pos().valid());
  EXPECT_EQ(s.pos().line, 1u);
  EXPECT_EQ(s.pos().column, 23u);
  EXPECT_EQ(s.ToString(), "SyntaxError: line 1:23: unexpected token \"FROMM\"");
}

// An Ok status has no representable message, so constructing one with a message
// must not produce an object whose ok() disagrees with its contents.
TEST(StatusTest, OkCodeWithMessageStaysOk) {
  const Status s(StatusCode::kOk, "ignored");
  EXPECT_TRUE(s.ok());
  EXPECT_TRUE(s.message().empty());
}

TEST(StatusTest, CopyIsDeep) {
  const Status original = IoError("disk fell over");
  Status copy = original;  // NOLINT(performance-unnecessary-copy-initialization)
  EXPECT_EQ(copy, original);
  copy = NotFound("gone");
  EXPECT_EQ(original.message(), "disk fell over");
  EXPECT_EQ(copy.message(), "gone");
}

TEST(StatusTest, MoveLeavesSourceOk) {
  Status source = Internal("boom");
  const Status moved = std::move(source);
  EXPECT_EQ(moved.code(), StatusCode::kInternal);
  EXPECT_TRUE(source.ok());  // NOLINT(bugprone-use-after-move) — moved-from state is the assertion
}

TEST(StatusTest, SelfAssignmentIsSafe) {
  Status s = TypeError("bad cast");
  const Status* alias = &s;
  s = *alias;
  EXPECT_EQ(s.message(), "bad cast");
}

TEST(StatusTest, EqualityComparesCodeMessageAndPosition) {
  EXPECT_EQ(NotFound("a"), NotFound("a"));
  EXPECT_NE(NotFound("a"), NotFound("b"));
  EXPECT_NE(NotFound("a"), Internal("a"));
  EXPECT_NE(SyntaxError("a", SourcePos{1, 1}), SyntaxError("a", SourcePos{1, 2}));
  EXPECT_NE(Status::Ok(), NotFound("a"));
}

TEST(StatusTest, EveryCodeHasADistinctName) {
  const std::vector<StatusCode> all = {
      StatusCode::kOk,           StatusCode::kNotFound,     StatusCode::kAlreadyExists,
      StatusCode::kInvalidArgument, StatusCode::kSyntaxError, StatusCode::kTypeError,
      StatusCode::kIoError,      StatusCode::kCorruption,   StatusCode::kIncompatible,
      StatusCode::kOutOfMemory,  StatusCode::kOutOfRange,   StatusCode::kSerializationFailure,
      StatusCode::kNotSupported, StatusCode::kInternal,
  };
  std::vector<std::string> names;
  for (const StatusCode code : all) {
    const std::string name = StatusCodeName(code);
    EXPECT_NE(name, "Unknown") << "code " << static_cast<int>(code);
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  EXPECT_EQ(std::unique(names.begin(), names.end()), names.end()) << "duplicate status names";
}

TEST(StatusOrTest, HoldsAValue) {
  const StatusOr<int> v = 42;
  ASSERT_TRUE(v.ok());
  EXPECT_EQ(v.value(), 42);
  EXPECT_EQ(*v, 42);
}

TEST(StatusOrTest, HoldsAnError) {
  const StatusOr<int> v = OutOfRange("too big");
  ASSERT_FALSE(v.ok());
  EXPECT_EQ(v.status().code(), StatusCode::kOutOfRange);
}

TEST(StatusOrTest, WorksWithMoveOnlyTypes) {
  StatusOr<std::unique_ptr<int>> v = std::make_unique<int>(7);
  ASSERT_TRUE(v.ok());
  const std::unique_ptr<int> taken = std::move(v).value();
  ASSERT_NE(taken, nullptr);
  EXPECT_EQ(*taken, 7);
}

TEST(StatusOrTest, ArrowReachesMembers) {
  const StatusOr<std::string> v = std::string("hello");
  ASSERT_TRUE(v.ok());
  EXPECT_EQ(v->size(), 5u);
}

namespace {

Status FailsWith(StatusCode code) { return Status(code, "propagated"); }

Status UsesReturnIfError(bool fail) {
  NANOSQL_RETURN_IF_ERROR(fail ? FailsWith(StatusCode::kIoError) : Status::Ok());
  return Status::Ok();
}

StatusOr<int> MaybeInt(bool fail) {
  if (fail) return NotFound("nope");
  return 5;
}

StatusOr<int> UsesAssignOrReturn(bool fail) {
  NANOSQL_ASSIGN_OR_RETURN(const int n, MaybeInt(fail));
  return n * 2;
}

}  // namespace

TEST(StatusMacroTest, ReturnIfErrorPropagatesOnlyFailures) {
  EXPECT_TRUE(UsesReturnIfError(false).ok());
  EXPECT_EQ(UsesReturnIfError(true).code(), StatusCode::kIoError);
}

TEST(StatusMacroTest, AssignOrReturnUnwrapsOrPropagates) {
  const StatusOr<int> good = UsesAssignOrReturn(false);
  ASSERT_TRUE(good.ok());
  EXPECT_EQ(good.value(), 10);

  const StatusOr<int> bad = UsesAssignOrReturn(true);
  ASSERT_FALSE(bad.ok());
  EXPECT_EQ(bad.status().code(), StatusCode::kNotFound);
}

}  // namespace
}  // namespace nanosql
