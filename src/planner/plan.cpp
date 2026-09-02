#include "planner/plan.h"
namespace tuplestone {
std::string PlanNode::Explain(size_t depth) const {
  std::string result(depth * 2, ' ');
  result += operation;
  result.push_back('\n');
  for (const auto& child : children)
    if (child) result += child->Explain(depth + 1);
  return result;
}
}  // namespace tuplestone
