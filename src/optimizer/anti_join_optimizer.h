#ifndef ANTI_JOIN_OPTIMIZER_H
#define ANTI_JOIN_OPTIMIZER_H

#include <memory>
#include <vector>

#include "sql_ast/expr_visitor.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {
namespace sql::ast {

/**
 * Rewrites `(c1, ..., ck) NOT IN (S)` into `NOT EXISTS (SELECT 1 FROM ... WHERE s1 = c1 AND ... AND sk = ck)`, which
 * is equivalent in the absence of nulls. The correlated form lets the engine evaluate S only for the rows that reach
 * the negation, instead of computing all of S first: in `F and not G`, the formula F becomes the bound of G.
 * When S is a plain projection, its FROM and WHERE are inlined into the EXISTS. Sources of the EXISTS that then map
 * onto sources of the enclosing query, as in the minimization of conjunctive queries, are removed: the translation
 * of S binds its free variables with copies of the relations the enclosing query already reads.
 */
class AntiJoinOptimizer : public ExpressionVisitor {
 public:
  using ExpressionVisitor::Visit;

  void Visit(Source& source) override;
  void Visit(From& from) override;
  void Visit(LogicalCondition& logical_condition) override;

 private:
  // The NOT EXISTS equivalent of a condition, or the condition itself.
  std::shared_ptr<Condition> Rewrite(const std::shared_ptr<Condition>& condition);

  // The FROMs enclosing the condition being visited, innermost last.
  std::vector<From*> scopes_;
};

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // ANTI_JOIN_OPTIMIZER_H
