#ifndef PREPROCESSING_LIT_VISITOR_REL_H
#define PREPROCESSING_LIT_VISITOR_REL_H

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

class LiteralVisitor : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;
  LiteralVisitor() = default;

  std::shared_ptr<RelDef> Visit(const std::shared_ptr<RelDef>& node) override;

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelLiteral>& node) override;

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelProduct>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelAbstractionExpr>& node) override;
  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelNumTerm>& node) override;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_LIT_VISITOR_REL_H
