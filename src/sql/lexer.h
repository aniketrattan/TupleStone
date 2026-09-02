#ifndef TUPLESTONE_SQL_LEXER_H_
#define TUPLESTONE_SQL_LEXER_H_
#include <string>
#include <string_view>
#include <vector>
#include "tuplestone/status.h"
namespace tuplestone {
enum class SqlTokenKind { kWord, kNumber, kString, kOperator, kPunctuation, kParameter };
struct SqlToken {
  SqlTokenKind kind;
  std::string text;
  SourcePos pos;
};
StatusOr<std::vector<SqlToken>> LexSql(std::string_view sql);
}  // namespace tuplestone
#endif
