#ifndef TUPLESTONE_SQL_PARSER_H_
#define TUPLESTONE_SQL_PARSER_H_
#include <string>
#include <string_view>
#include "tuplestone/status.h"
namespace tuplestone {
struct SqlStatement {
  std::string source;
  std::string kind;
};
StatusOr<SqlStatement> ParseSql(std::string_view sql);
}  // namespace tuplestone
#endif
