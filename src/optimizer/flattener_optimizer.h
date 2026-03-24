#ifndef FLATTENER_OPTIMIZER_H
#define FLATTENER_OPTIMIZER_H

#include "base_optimizer.h"

namespace rel2sql {
namespace sql::ast {

class FlattenerOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(Select& select) override;

  // If the Select wraps a Union/UnionAll with a simple projection, returns the
  // flattened Union (projection pushed into members). Otherwise returns nullptr.
  static std::shared_ptr<Expression> TryFlattenUnionSubquery(
      const std::shared_ptr<Select>& select);

 private:
  bool TryFlattenSubquery(Select& select);

  // Helper functions for subquery flattening

  // Check if a source can be flattened into a subquery.
  static bool CanFlattenSubquery(const std::shared_ptr<Source>& source);

  // Check if a source is a constant-only subquery (SELECT const AS col [, ...] with no FROM).
  static bool CanFlattenConstantSubquery(const std::shared_ptr<Source>& source);

  // Build a map of term names to terms in the subquery.
  static std::unordered_map<std::string, std::shared_ptr<Term>> BuildTermMap(const std::shared_ptr<Select>& subquery);

  // Merge WHERE conditions of the outer and subquery.
  static void MergeWhereConditions(From& outer_from, const std::shared_ptr<Condition>& subquery_where);

  // Merge subquery CTEs into the outer SELECT scope.
  static void MergeCTEs(Select& outer_select, const std::shared_ptr<Select>& subquery);
};  // class FlattenerOptimizer

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // FLATTENER_OPTIMIZER_H
