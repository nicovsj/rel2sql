#ifndef PREPROCESSING_RECURSION_VISITOR_REL_H
#define PREPROCESSING_RECURSION_VISITOR_REL_H

#include <memory>
#include <unordered_set>
#include <vector>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_container.h"
#include "rel_ast/rel_ast_visitor.h"
#include "rel_ast/relation_info.h"

namespace rel2sql {

class RecursionVisitor : public RelASTVisitor {
 public:
  explicit RecursionVisitor(RelASTContainer* container) : container_(container) {}

  void Visit(RelProgram& node) override;
  void Visit(RelDef& node) override;
  void Visit(RelAbstraction& node) override;
  void Visit(RelBindingsFormula& node) override;
  void Visit(RelBinOp& node) override;
  void Visit(RelQuantification& node) override;
  void Visit(RelFullAppl& node) override;

 private:
  struct RecursiveBranchMatch {
    std::shared_ptr<RelQuantification> exists_clause;
    std::shared_ptr<RelFullAppl> recursive_call;
    std::shared_ptr<RelFormula> residual_formula;
  };

  struct RecursionPatternMatch {
    std::vector<std::shared_ptr<RelFormula>> base_disjuncts;
    std::vector<RecursiveBranchMatch> recursive_disjuncts;
  };

  std::string current_q_;
  RelASTContainer* container_;

  bool IsRecursiveID(const std::string& id) const;
  std::unordered_set<std::string> CollectIDs(const std::shared_ptr<RelFormula>& formula) const;
  bool OnlyEDBsOrNonRecursiveIDBs(const std::unordered_set<std::string>& ids,
                                  const std::string& current_q) const;
  bool VariablesFromBindingOrQuantification(
      const std::set<std::string>& vars,
      const std::vector<std::shared_ptr<RelBinding>>& outer_bindings,
      const std::vector<std::shared_ptr<RelBinding>>& quant_bindings) const;
  bool CheckRecursionPattern(const std::shared_ptr<RelFormula>& formula,
                             const std::vector<std::shared_ptr<RelBinding>>& bindings,
                             RecursionPatternMatch& match);
  bool CheckExistsPattern(const std::shared_ptr<RelQuantification>& quant,
                          const std::string& q,
                          const std::vector<std::shared_ptr<RelBinding>>& outer_bindings,
                          RecursiveBranchMatch& match);
  bool IsCallToQ(const RelFullAppl& appl, const std::string& q) const;
  void CollectOrDisjuncts(const std::shared_ptr<RelFormula>& formula,
                          std::vector<std::shared_ptr<RelFormula>>& disjuncts) const;
  void FindAndPatternParts(const std::shared_ptr<RelFormula>& formula,
                           const std::string& q,
                           std::shared_ptr<RelFullAppl>& q_call,
                           std::shared_ptr<RelFormula>& f_part) const;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_RECURSION_VISITOR_REL_H
