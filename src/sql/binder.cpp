#include "sql/binder.h"
namespace tuplestone {
StatusOr<LogicalPlan> BindSql(const SqlStatement& statement) {
  if (statement.source.empty()) return InvalidArgument("empty SQL statement");
  return LogicalPlan{statement.source, statement.kind};
}
}  // namespace tuplestone
