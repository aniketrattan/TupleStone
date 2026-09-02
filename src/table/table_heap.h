#ifndef TUPLESTONE_TABLE_TABLE_HEAP_H_
#define TUPLESTONE_TABLE_TABLE_HEAP_H_

#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "common/types.h"
#include "tuplestone/status.h"
#include "tuplestone/value.h"

namespace tuplestone {

class Tuple {
 public:
  Tuple() = default;
  explicit Tuple(std::vector<Value> values) : values_(std::move(values)) {}
  const std::vector<Value>& values() const { return values_; }
  std::vector<Value>& values() { return values_; }
  size_t size() const { return values_.size(); }
  const Value& operator[](size_t index) const { return values_[index]; }

 private:
  std::vector<Value> values_;
};

class TableHeap {
 public:
  TableHeap() = default;
  StatusOr<RID> InsertTuple(const Tuple& tuple);
  StatusOr<Tuple> GetTuple(RID rid) const;
  Status UpdateTuple(RID rid, const Tuple& tuple);
  Status MarkDelete(RID rid);
  std::vector<std::pair<RID, Tuple>> Scan() const;
  size_t size() const;

 private:
  mutable std::mutex mutex_;
  std::map<RID, Tuple> tuples_;
  page_id_t next_page_id_ = 1;
  slot_id_t next_slot_id_ = 0;
};

}  // namespace tuplestone

#endif
