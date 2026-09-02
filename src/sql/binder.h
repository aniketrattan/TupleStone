#ifndef TUPLESTONE_SQL_BINDER_H_
#define TUPLESTONE_SQL_BINDER_H_
#include <string>
#include "tuplestone/status.h"
#include "sql/parser.h"
namespace tuplestone {
struct LogicalPlan {
  std::string sql;
  std::string operation;
};
StatusOr<LogicalPlan> BindSql(const SqlStatement& statement);
}  // namespace tuplestone
#endif
