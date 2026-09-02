#include "sql/lexer.h"

#include <cctype>
#include <utility>

namespace tuplestone {

StatusOr<std::vector<SqlToken>> LexSql(std::string_view sql) {
  std::vector<SqlToken> result;
  uint32_t line = 1;
  uint32_t column = 1;
  size_t i = 0;
  while (i < sql.size()) {
    if (std::isspace(static_cast<unsigned char>(sql[i])) != 0) {
      if (sql[i] == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
      ++i;
      continue;
    }
    const SourcePos pos{line, column};
    const size_t start = i;
    if (std::isalpha(static_cast<unsigned char>(sql[i])) != 0 || sql[i] == '_') {
      ++i;
      while (i < sql.size() &&
             (std::isalnum(static_cast<unsigned char>(sql[i])) != 0 || sql[i] == '_'))
        ++i;
      result.push_back({SqlTokenKind::kWord, std::string(sql.substr(start, i - start)), pos});
      column += static_cast<uint32_t>(i - start);
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(sql[i])) != 0) {
      ++i;
      while (i < sql.size() &&
             (std::isdigit(static_cast<unsigned char>(sql[i])) != 0 || sql[i] == '.'))
        ++i;
      result.push_back({SqlTokenKind::kNumber, std::string(sql.substr(start, i - start)), pos});
      column += static_cast<uint32_t>(i - start);
      continue;
    }
    if (sql[i] == '\'' || sql[i] == '"') {
      const char quote = sql[i++];
      std::string value;
      while (i < sql.size() && sql[i] != quote) value.push_back(sql[i++]);
      if (i == sql.size()) return SyntaxError("unterminated quoted literal", pos);
      ++i;
      const uint32_t consumed = static_cast<uint32_t>(value.size() + 2);
      result.push_back(
          {quote == '\'' ? SqlTokenKind::kString : SqlTokenKind::kWord, std::move(value), pos});
      column += consumed;
      continue;
    }
    if (sql[i] == '?') {
      result.push_back({SqlTokenKind::kParameter, "?", pos});
      ++i;
      ++column;
      continue;
    }
    if (std::string_view("(),;=<>+-*/%").find(sql[i]) != std::string_view::npos) {
      const bool punctuation = std::string_view("(),;").find(sql[i]) != std::string_view::npos;
      result.push_back({punctuation ? SqlTokenKind::kPunctuation : SqlTokenKind::kOperator,
                        std::string(1, sql[i]), pos});
      ++i;
      ++column;
      continue;
    }
    return SyntaxError("unexpected character", pos);
  }
  return result;
}

}  // namespace tuplestone
