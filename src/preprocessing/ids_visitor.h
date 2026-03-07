#ifndef PREPROCESSING_IDS_VISITOR_REL_H
#define PREPROCESSING_IDS_VISITOR_REL_H

#include <unordered_set>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_visitor.h"
#include "rel_ast/rel_context_builder.h"

namespace rel2sql {

class IDsVisitor : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;
  explicit IDsVisitor(RelContextBuilder* builder) : builder_(builder) {}

  std::shared_ptr<RelProgram> Visit(const std::shared_ptr<RelProgram>& node) override;
  std::shared_ptr<RelDef> Visit(const std::shared_ptr<RelDef>& node) override;

  std::shared_ptr<RelAbstraction> Visit(const std::shared_ptr<RelAbstraction>& node) override;

  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelIDTerm>& node) override;

  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelExpressionAbstraction>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelFormulaAbstraction>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelPartialApplication>& node) override;

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullApplication>& node) override;

 private:
  using StringSet = std::unordered_set<std::string>;

  void AddDepsFromBase(const std::shared_ptr<RelApplBase>& base);
  void AddDepsFromParams(const std::vector<std::shared_ptr<RelApplParam>>& params);
  void AddDepsFromBindings(const std::vector<std::shared_ptr<RelBinding>>& bindings);

  RelContextBuilder* builder_;
  StringSet deps_;
  std::string current_def_id_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_IDS_VISITOR_REL_H
