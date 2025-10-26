#ifndef SELF_JOIN_OPTIMIZER_H
#define SELF_JOIN_OPTIMIZER_H

#include <unordered_map>

#include "base_optimizer.h"

namespace rel2sql {
namespace sql::ast {

class SelfJoinOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(SelectStatement& select_statement) override;

 private:
  bool EliminateRedundantSelfJoins(SelectStatement& select_statement);
  std::unordered_map<std::string, std::vector<std::shared_ptr<Source>>> GroupSourcesByTable(
      const std::vector<std::shared_ptr<Source>>& sources);
  void CollectComparisonConditions(const std::shared_ptr<Condition>& condition,
                                   std::vector<std::shared_ptr<ComparisonCondition>>& comparisons);
};  // class SelfJoinOptimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // SELF_JOIN_OPTIMIZER_H
