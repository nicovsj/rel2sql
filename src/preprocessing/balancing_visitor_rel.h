#ifndef PREPROCESSING_BALANCING_VISITOR_REL_H
#define PREPROCESSING_BALANCING_VISITOR_REL_H

#include <expected>
#include <set>
#include <vector>

#include "rel_ast/rel_ast.h"
#include "support/exceptions.h"
#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

class BalancingVisitorRel : public RelASTVisitor {
 public:
  BalancingVisitorRel() = default;

  void Visit(RelProgram& node) override;
  void Visit(RelDef& node) override;
  void Visit(RelAbstraction& node) override;
  void Visit(RelProductExpr& node) override;
  void Visit(RelConditionExpr& node) override;
  void Visit(RelAbstractionExpr& node) override;
  void Visit(RelFormulaExpr& node) override;
  void Visit(RelBindingsExpr& node) override;
  void Visit(RelBindingsFormula& node) override;
  void Visit(RelPartialAppl& node) override;
  void Visit(RelFullAppl& node) override;
  void Visit(RelBinOp& node) override;
  void Visit(RelUnOp& node) override;
  void Visit(RelQuantification& node) override;
  void Visit(RelParen& node) override;

 private:
  void CollectComparatorConjuncts(const std::shared_ptr<RelFormula>& formula,
                                  std::vector<std::shared_ptr<RelNode>>& comparator_conjuncts,
                                  std::vector<std::shared_ptr<RelNode>>& non_comparator_conjuncts);
  void CollectNegatedConjuncts(const std::shared_ptr<RelFormula>& formula,
                               std::vector<std::shared_ptr<RelNode>>& negated_conjuncts,
                               std::vector<std::shared_ptr<RelNode>>& non_negated_conjuncts);
  std::expected<void, std::pair<std::string, SourceLocation>> ValidateFreeVariables(
      const std::vector<std::shared_ptr<RelNode>>& checked_conjuncts,
      const std::vector<std::shared_ptr<RelNode>>& reference_conjuncts);
  void ValidateComparatorFreeVariables(
      const std::vector<std::shared_ptr<RelNode>>& comparator_conjuncts,
      const std::vector<std::shared_ptr<RelNode>>& non_comparator_conjuncts);
  void ValidateNegatedFreeVariables(
      const std::vector<std::shared_ptr<RelNode>>& negated_conjuncts,
      const std::vector<std::shared_ptr<RelNode>>& non_negated_conjuncts);

  std::vector<std::set<std::string>> condition_lhs_free_vars_stack_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_BALANCING_VISITOR_REL_H
