#include "sql/parser.h"
#include <algorithm>
#include <cctype>
#include "sql/lexer.h"
namespace tuplestone {
StatusOr<SqlStatement> ParseSql(std::string_view sql) {
  auto tokens = LexSql(sql);
  if (!tokens.ok()) return tokens.status();
  if (tokens->empty()) return SyntaxError("empty SQL statement");
  std::string kind = tokens->front().text;
  std::transform(kind.begin(), kind.end(), kind.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return SqlStatement{std::string(sql), std::move(kind)};
}
}  // namespace tuplestone
