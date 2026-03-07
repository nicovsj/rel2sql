#ifndef REWRITER_UNDERSCORE_REWRITER_H
#define REWRITER_UNDERSCORE_REWRITER_H

#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

class RelContextBuilder;

/**
 * Rewrites wildcards in applications:
 *
 * 1. Full application: A(..., _, ...) =>
 *    exists((z) | A(..., z, ...))
 *
 * 2. Partial application: A[arg1, ..., argi-1, _, argi+1, ..., argk] =>
 *   (zk+1, ..., z|A|) : exists((z) | A(arg1, ..., z, ..., argk, zk+1, ..., z|A|))
 */
class WildcardRewriter : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;

  WildcardRewriter() : container_(nullptr) {}
  explicit WildcardRewriter(const RelContextBuilder* container) : container_(container) {}

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullAppl>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelPartialAppl>& node) override;

 private:
  std::string FreshVarName();
  std::shared_ptr<RelApplParam> MakeVarParam(const std::string& var);
  int GetRelationArity(const std::string& id) const;

  const RelContextBuilder* container_;
  int fresh_var_counter_ = 0;
};

}  // namespace rel2sql

#endif  // REWRITER_UNDERSCORE_REWRITER_H
