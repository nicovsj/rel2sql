#ifndef PREPROCESSING_ARITY_VISITOR_REL_H
#define PREPROCESSING_ARITY_VISITOR_REL_H

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_visitor.h"
#include "rel_ast/rel_context_builder.h"

namespace rel2sql {

class ArityVisitor : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;
  explicit ArityVisitor(RelContextBuilder* container) : container_(container) {}

  std::shared_ptr<RelProgram> Visit(const std::shared_ptr<RelProgram>& node) override;
  std::shared_ptr<RelDef> Visit(const std::shared_ptr<RelDef>& node) override;

  std::shared_ptr<RelAbstraction> Visit(const std::shared_ptr<RelAbstraction>& node) override;

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelLiteral>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelProduct>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelConditionExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelAbstractionExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelFormulaExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBindingsExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBindingsFormula>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelPartialAppl>& node) override;

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullAppl>& node) override;

  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelIDTerm>& node) override;
  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelNumTerm>& node) override;
  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelOpTerm>& node) override;
  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelParenthesisTerm>& node) override;

 private:
  int GetArityFromBase(const std::shared_ptr<RelApplBase>& base);
  int GetArityFromParams(const std::vector<std::shared_ptr<RelApplParam>>& params);

  RelContextBuilder* container_;
  std::unordered_map<std::string, std::vector<std::shared_ptr<RelDef>>> defs_by_id_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_ARITY_VISITOR_REL_H
