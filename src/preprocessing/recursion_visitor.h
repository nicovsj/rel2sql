#ifndef PREPROCESSING_RECURSION_VISITOR_REL_H
#define PREPROCESSING_RECURSION_VISITOR_REL_H

#include <memory>
#include <unordered_set>
#include <vector>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_visitor.h"
#include "rel_ast/rel_context_builder.h"
#include "rel_ast/relation_info.h"

namespace rel2sql {

class RecursionVisitor : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;
  explicit RecursionVisitor(RelContextBuilder* container) : container_(container) {}

  std::shared_ptr<RelProgram> Visit(const std::shared_ptr<RelProgram>& node) override;
  std::shared_ptr<RelDef> Visit(const std::shared_ptr<RelDef>& node) override;

  std::shared_ptr<RelUnion> Visit(const std::shared_ptr<RelUnion>& node) override;

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelFormulaAbstraction>& node) override;

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullApplication>& node) override;

 private:
  struct RecursiveBranchMatch {
    std::shared_ptr<RelExistential> exists_clause;
    std::shared_ptr<RelFullApplication> recursive_call;
    std::shared_ptr<RelFormula> residual_formula;
  };

  struct RecursionPatternMatch {
    std::vector<std::shared_ptr<RelFormula>> base_disjuncts;
    std::vector<RecursiveBranchMatch> recursive_disjuncts;
  };

  std::string current_q_;
  RelContextBuilder* container_;

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
  bool CheckExistsPattern(const std::shared_ptr<RelExistential>& exists,
                          const std::string& q,
                          const std::vector<std::shared_ptr<RelBinding>>& outer_bindings,
                          RecursiveBranchMatch& match);
  bool IsCallToQ(const RelFullApplication& appl, const std::string& q) const;
  void CollectOrDisjuncts(const std::shared_ptr<RelFormula>& formula,
                          std::vector<std::shared_ptr<RelFormula>>& disjuncts) const;
  void FindAndPatternParts(const std::shared_ptr<RelFormula>& formula,
                           const std::string& q,
                           std::shared_ptr<RelFullApplication>& q_call,
                           std::shared_ptr<RelFormula>& f_part) const;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_RECURSION_VISITOR_REL_H
