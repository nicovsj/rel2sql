#ifndef PREPROCESSING_IDS_VISITOR_REL_H
#define PREPROCESSING_IDS_VISITOR_REL_H

#include <unordered_set>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_context.h"
#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

class IDsVisitor : public RelASTVisitor {
 public:
  explicit IDsVisitor(RelContext* container) : container_(container) {}

  void Visit(RelProgram& node) override;
  void Visit(RelDef& node) override;
  void Visit(RelAbstraction& node) override;
  void Visit(RelIDTerm& node) override;
  void Visit(RelNumTerm& node) override;
  void Visit(RelOpTerm& node) override;
  void Visit(RelParenthesisTerm& node) override;
  void Visit(RelLitExpr& node) override;
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
  using StringSet = std::unordered_set<std::string>;

  void AddDepsFromBase(const std::shared_ptr<RelApplBase>& base);
  void AddDepsFromParams(const std::vector<std::shared_ptr<RelApplParam>>& params);
  void AddDepsFromBindings(const std::vector<std::shared_ptr<RelBinding>>& bindings);

  RelContext* container_;
  StringSet deps_;
  std::string current_def_id_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_IDS_VISITOR_REL_H
