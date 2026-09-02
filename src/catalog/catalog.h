#ifndef TUPLESTONE_CATALOG_CATALOG_H_
#define TUPLESTONE_CATALOG_CATALOG_H_
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include "common/types.h"
#include "tuplestone/status.h"
#include "tuplestone/value.h"
namespace tuplestone {
struct TableInfo {
  table_id_t id = 0;
  std::string name;
  Schema schema;
};
class Catalog {
 public:
  Status CreateTable(std::string_view name, Schema schema);
  Status DropTable(std::string_view name);
  StatusOr<TableInfo> FindTable(std::string_view name) const;
  std::vector<TableInfo> Tables() const;

 private:
  mutable std::mutex mutex_;
  table_id_t next_id_ = 1;
  std::map<std::string, TableInfo, std::less<>> tables_;
};
}  // namespace tuplestone
#endif
