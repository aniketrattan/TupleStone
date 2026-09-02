#include "table/table_heap.h"

namespace tuplestone {
StatusOr<RID> TableHeap::InsertTuple(const Tuple& tuple) {
  std::lock_guard lock(mutex_);
  if (tuple.size() == 0) return InvalidArgument("tuple has no columns");
  RID rid{next_page_id_, next_slot_id_++};
  if (next_slot_id_ == 0) ++next_page_id_;
  // Materialize the copy before insertion so GCC does not instantiate the
  // problematic pair-copy path for Tuple's variant-backed values.
  tuples_.emplace(rid, Tuple(tuple));
  return rid;
}
StatusOr<Tuple> TableHeap::GetTuple(RID rid) const {
  std::lock_guard lock(mutex_);
  auto it = tuples_.find(rid);
  if (it == tuples_.end()) return NotFound("tuple not found");
  return it->second;
}
Status TableHeap::UpdateTuple(RID rid, const Tuple& tuple) {
  std::lock_guard lock(mutex_);
  auto it = tuples_.find(rid);
  if (it == tuples_.end()) return NotFound("tuple not found");
  it->second = tuple;
  return Status::Ok();
}
Status TableHeap::MarkDelete(RID rid) {
  std::lock_guard lock(mutex_);
  auto it = tuples_.find(rid);
  if (it == tuples_.end()) return NotFound("tuple not found");
  tuples_.erase(it);
  return Status::Ok();
}
std::vector<std::pair<RID, Tuple>> TableHeap::Scan() const {
  std::lock_guard lock(mutex_);
  std::vector<std::pair<RID, Tuple>> result;
  result.reserve(tuples_.size());
  for (const auto& entry : tuples_) result.push_back(entry);
  return result;
}
size_t TableHeap::size() const {
  std::lock_guard lock(mutex_);
  return tuples_.size();
}
}  // namespace tuplestone
