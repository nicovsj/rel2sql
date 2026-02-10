#ifndef PREPROCESSING_SAFE_VISITOR_REL_H
#define PREPROCESSING_SAFE_VISITOR_REL_H

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_container.h"
#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

class SafeVisitorRel : public RelASTVisitor {
 public:
  explicit SafeVisitorRel(RelASTContainer* container) : container_(container) {}

  void Visit(RelProgram& node) override;
  void Visit(RelDef& node) override;
  void Visit(RelAbstraction& node) override;
  void Visit(RelTermExpr& node) override;
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
  void Visit(RelComparison& node) override;

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

  RelASTContainer* container_;
  std::string current_relation_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_SAFE_VISITOR_REL_H
