#ifndef TUPLESTONE_EXEC_EXECUTOR_H_
#define TUPLESTONE_EXEC_EXECUTOR_H_
#include <functional>
#include <utility>
#include <vector>
#include "tuplestone/status.h"
#include "tuplestone/value.h"
namespace tuplestone {
class Executor {
 public:
  using Row = std::vector<Value>;
  explicit Executor(std::function<StatusOr<std::vector<Row>>()> source)
      : source_(std::move(source)) {}
  StatusOr<std::vector<Row>> Run() { return source_(); }

 private:
  std::function<StatusOr<std::vector<Row>>()> source_;
};
}  // namespace tuplestone
#endif
