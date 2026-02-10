#ifndef PREPROCESSING_ARITY_VISITOR_REL_H
#define PREPROCESSING_ARITY_VISITOR_REL_H

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_container.h"
#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

class ArityVisitorRel : public RelASTVisitor {
 public:
  explicit ArityVisitorRel(RelASTContainer* container) : container_(container) {}

  void Visit(RelProgram& node) override;
  void Visit(RelDef& node) override;
  void Visit(RelAbstraction& node) override;
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
  void Visit(RelIDTerm& node) override;
  void Visit(RelNumTerm& node) override;
  void Visit(RelOpTerm& node) override;
  void Visit(RelParenthesisTerm& node) override;

 private:
  int GetArityFromBase(const std::shared_ptr<RelApplBase>& base);
  int GetArityFromParams(const std::vector<std::shared_ptr<RelApplParam>>& params);

  RelASTContainer* container_;
  std::unordered_map<std::string, std::vector<std::shared_ptr<RelDef>>> defs_by_id_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_ARITY_VISITOR_REL_H
