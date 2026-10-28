#ifndef KEY_OPTIMIZER_H
#define KEY_OPTIMIZER_H

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sql_ast/expr_visitor.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {
namespace sql::ast {

/**
 * Uses the keys declared for the base relations to remove the deduplication that set semantics makes the
 * translation add.
 *
 * Keys are propagated bottom-up: a view or subquery gets a key when its rows are determined by some of its output
 * columns. The pass drops DISTINCT when the output columns of a SELECT determine a key of every source, since the
 * rows of the join are then already distinct on the output (e.g. the projections a Rel aggregate ranges over, which
 * keep the key of the aggregated tuples).
 */
class KeyOptimizer : public ExpressionVisitor {
 public:
  using ExpressionVisitor::Visit;

  // A key is a set of column names; a relation may have several. An empty key means at most one row.
  using Keys = std::vector<std::vector<std::string>>;
  using KeyMap = std::unordered_map<std::string, Keys>;

  explicit KeyOptimizer(KeyMap keys) : keys_(std::move(keys)) {}

  void Visit(View& view) override;
  void Visit(Select& select) override;

 private:
  std::optional<Keys> SourceKeys(const Source& source) const;

  KeyMap keys_;                                          // keys of base relations and of the views seen so far
  std::unordered_map<const Select*, Keys> select_keys_;  // keys of the SELECTs visited, by output column name
};

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // KEY_OPTIMIZER_H
