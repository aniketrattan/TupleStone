#ifndef TUPLESTONE_PLANNER_PLAN_H_
#define TUPLESTONE_PLANNER_PLAN_H_
#include <memory>
#include <string>
#include <vector>
namespace tuplestone {
struct PlanNode {
  std::string operation;
  std::vector<std::shared_ptr<PlanNode>> children;
  std::string Explain(size_t depth = 0) const;
};
}  // namespace tuplestone
#endif
