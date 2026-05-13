#ifndef PREPROCESSING_BUILTIN_RESOLVER_H
#define PREPROCESSING_BUILTIN_RESOLVER_H

#include <memory>

#include "rel_ast/rel_ast_visitor.h"
#include "rel_ast/rel_context_builder.h"

namespace rel2sql {

// Lowers known prelude-style partial/full applications to dedicated Rel AST nodes
// (aggregates, ordering, dates, decimals, strings) before translation.
class BuiltinResolver : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;

  explicit BuiltinResolver(RelContextBuilder* builder) : builder_(builder) {}

  std::shared_ptr<RelNode> Resolve(std::shared_ptr<RelNode> root) { return Visit(std::move(root)); }

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelPartialApplication>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullApplication>& node) override;

 private:
  std::shared_ptr<RelExpr> TryLowerPartial(const std::shared_ptr<RelPartialApplication>& node);
  std::shared_ptr<RelFormula> TryLowerFull(const std::shared_ptr<RelFullApplication>& node);

  RelContextBuilder* builder_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_BUILTIN_RESOLVER_H
