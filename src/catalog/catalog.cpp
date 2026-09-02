#include "catalog/catalog.h"
namespace tuplestone {
Status Catalog::CreateTable(std::string_view name, Schema schema) {
  std::lock_guard lock(mutex_);
  const std::string key(name);
  if (tables_.contains(key)) return AlreadyExists("table already exists");
  tables_.emplace(key, TableInfo{next_id_++, key, std::move(schema)});
  return Status::Ok();
}
Status Catalog::DropTable(std::string_view name) {
  std::lock_guard lock(mutex_);
  if (tables_.erase(std::string(name)) == 0) return NotFound("table does not exist");
  return Status::Ok();
}
StatusOr<TableInfo> Catalog::FindTable(std::string_view name) const {
  std::lock_guard lock(mutex_);
  auto it = tables_.find(name);
  return it == tables_.end() ? StatusOr<TableInfo>(NotFound("table does not exist"))
                             : StatusOr<TableInfo>(it->second);
}
std::vector<TableInfo> Catalog::Tables() const {
  std::lock_guard lock(mutex_);
  std::vector<TableInfo> result;
  for (const auto& [name, table] : tables_) {
    (void)name;
    result.push_back(table);
  }
  return result;
}
}  // namespace tuplestone
