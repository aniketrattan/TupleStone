#ifndef TUPLESTONE_INDEX_B_PLUS_TREE_H_
#define TUPLESTONE_INDEX_B_PLUS_TREE_H_

#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"
#include "tuplestone/status.h"

namespace tuplestone {

// A persistent-page-compatible ordered index facade. The public behaviour is
// intentionally independent of node fanout so callers can use the same API
// while the storage engine evolves its page layout.
class BPlusTree {
 public:
  explicit BPlusTree(bool unique = true) : unique_(unique) {}
  Status Insert(std::string_view key, RID rid);
  Status Remove(std::string_view key, RID rid = RID{});
  StatusOr<RID> Search(std::string_view key) const;
  std::vector<RID> SearchAll(std::string_view key) const;
  std::vector<std::pair<std::string, RID>> Range(std::string_view lower = {}) const;
  Status Validate() const;
  size_t size() const;

 private:
  mutable std::mutex mutex_;
  bool unique_;
  std::map<std::string, std::vector<RID>, std::less<>> entries_;
};

}  // namespace tuplestone

#endif
