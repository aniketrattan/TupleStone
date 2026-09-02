#include "index/b_plus_tree.h"

#include <algorithm>

namespace tuplestone {
Status BPlusTree::Insert(std::string_view key, RID rid) {
  if (!rid.valid()) return InvalidArgument("index RID is invalid");
  std::lock_guard lock(mutex_);
  auto& values = entries_[std::string(key)];
  if (unique_ && !values.empty()) return AlreadyExists("duplicate index key");
  if (std::find(values.begin(), values.end(), rid) != values.end())
    return AlreadyExists("duplicate index entry");
  values.push_back(rid);
  return Status::Ok();
}
Status BPlusTree::Remove(std::string_view key, RID rid) {
  std::lock_guard lock(mutex_);
  auto it = entries_.find(key);
  if (it == entries_.end()) return NotFound("index key not found");
  if (unique_ || !rid.valid()) {
    entries_.erase(it);
    return Status::Ok();
  }
  auto& values = it->second;
  values.erase(std::remove(values.begin(), values.end(), rid), values.end());
  if (values.empty()) entries_.erase(it);
  return Status::Ok();
}
StatusOr<RID> BPlusTree::Search(std::string_view key) const {
  std::lock_guard lock(mutex_);
  auto it = entries_.find(key);
  if (it == entries_.end() || it->second.empty()) return NotFound("index key not found");
  return it->second.front();
}
std::vector<RID> BPlusTree::SearchAll(std::string_view key) const {
  std::lock_guard lock(mutex_);
  auto it = entries_.find(key);
  return it == entries_.end() ? std::vector<RID>{} : it->second;
}
std::vector<std::pair<std::string, RID>> BPlusTree::Range(std::string_view lower) const {
  std::lock_guard lock(mutex_);
  std::vector<std::pair<std::string, RID>> result;
  for (auto it = entries_.lower_bound(lower); it != entries_.end(); ++it)
    for (const RID rid : it->second) result.emplace_back(it->first, rid);
  return result;
}
Status BPlusTree::Validate() const {
  std::lock_guard lock(mutex_);
  for (const auto& [key, values] : entries_) {
    if (key.empty() && values.empty()) return Corruption("empty index entry");
    if (unique_ && values.size() > 1) return Corruption("unique index contains duplicate key");
  }
  return Status::Ok();
}
size_t BPlusTree::size() const {
  std::lock_guard lock(mutex_);
  size_t count = 0;
  for (const auto& [key, values] : entries_) {
    (void)key;
    count += values.size();
  }
  return count;
}
}  // namespace tuplestone
