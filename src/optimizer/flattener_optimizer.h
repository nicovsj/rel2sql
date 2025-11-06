#ifndef FLATTENER_OPTIMIZER_H
#define FLATTENER_OPTIMIZER_H

#include "base_optimizer.h"

namespace rel2sql {
namespace sql::ast {

class FlattenerOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(SelectStatement& select_statement) override;

 private:
  bool TryFlattenSubquery(SelectStatement& select_statement);

  // Helper functions for subquery flattening
  static bool CanFlattenSubquery(const std::shared_ptr<Source>& source);
  static std::unordered_map<std::string, std::shared_ptr<Column>> BuildColumnMap(
      const std::shared_ptr<SelectStatement>& subquery);
  static void MergeWhereConditions(FromStatement& outer_from,
                                   const std::shared_ptr<Condition>& subquery_where);
};  // class FlattenerOptimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // FLATTENER_OPTIMIZER_H
