#ifndef PREPROCESSING_SAFE_VISITOR_REL_H
#define PREPROCESSING_SAFE_VISITOR_REL_H

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_visitor.h"
#include "rel_ast/rel_context_builder.h"

namespace rel2sql {

class SafetyVisitor : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;
  explicit SafetyVisitor(RelContextBuilder* container) : container_(container) {}

  std::shared_ptr<RelProgram> Visit(const std::shared_ptr<RelProgram>& node) override;

  std::shared_ptr<RelAbstraction> Visit(const std::shared_ptr<RelAbstraction>& node) override;

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelTermExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelProductExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelConditionExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelAbstractionExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelFormulaExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBindingsExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBindingsFormula>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelPartialAppl>& node) override;

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullAppl>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelConjunction>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelDisjunction>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelNegation>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelQuantification>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelParen>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelComparison>& node) override;

 private:
  void ComputeBindingsSafety(RelNode& current, RelNode& child,
                             const std::vector<std::shared_ptr<RelBinding>>& bindings);
  void ComputeIDApplicationSafety(RelNode& node,
                                  const std::vector<std::shared_ptr<RelApplParam>>& params,
                                  const std::string& id);
  void ComputeRelAbsApplicationSafety(RelNode& node, RelNode& base_node,
                                      const std::vector<std::shared_ptr<RelApplParam>>& params);
  void ExtractAndStoreVariableDomains(const BoundSet& safety);
  Projection ExtractSingleVariableProjection(const Projection& proj,
                                             size_t variable_index) const;

 protected:
  RelContextBuilder* GetContainer() const { return container_; }

 private:
  RelContextBuilder* container_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_SAFE_VISITOR_REL_H
