#ifndef SAFE_VISITOR_H
#define SAFE_VISITOR_H

#include <antlr4-runtime.h>

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "preprocessing/base_visitor.h"
#include "rel_ast/extended_ast.h"

namespace rel2sql {

class SafeVisitor : public BaseVisitor {
  /*
   * Visitor that computes the safeness analysis of each formula and expr in the Rel program.
   */
 public:
  explicit SafeVisitor(std::shared_ptr<RelAST> ast);

  std::any visitProgram(psr::ProgramContext* ctx) override;

  std::any visitRelDef(psr::RelDefContext* ctx) override;

  std::any visitRelAbs(psr::RelAbsContext* ctx) override;

  // Expression branches

  std::any visitIDExpr(psr::IDExprContext* ctx) override;

  std::any visitProductExpr(psr::ProductExprContext* ctx) override;

  std::any visitConditionExpr(psr::ConditionExprContext* ctx) override;

  std::any visitRelAbsExpr(psr::RelAbsExprContext* ctx) override;

  std::any visitFormulaExpr(psr::FormulaExprContext* ctx) override;

  std::any visitBindingsExpr(psr::BindingsExprContext* ctx) override;

  std::any visitBindingsFormula(psr::BindingsFormulaContext* ctx) override;

  std::any visitPartialAppl(psr::PartialApplContext* ctx) override;

  //  Formula branches

  std::any visitFullAppl(psr::FullApplContext* ctx) override;

  std::any visitBinOp(psr::BinOpContext* ctx) override;

  std::any visitUnOp(psr::UnOpContext* ctx) override;

  std::any visitQuantification(psr::QuantificationContext* ctx) override;

  std::any visitParen(psr::ParenContext* ctx) override;

  std::any visitComparison(psr::ComparisonContext* ctx) override;

  //  Binding branches

  // std::any visitBindingInner(psr::BindingInnerContext *ctx) override;

  // std::any visitBinding(psr::BindingContext *ctx) override;

  std::any visitApplBase(psr::ApplBaseContext* ctx) override;

  // std::any visitApplParams(psr::ApplParamsContext *ctx) override;

  std::any visitApplParam(psr::ApplParamContext* ctx) override;

 private:
  std::any VisitConjunction(psr::BinOpContext* ctx);

  std::any VisitDisjunction(psr::BinOpContext* ctx);

  std::vector<std::string> ExtractHeadVariables(psr::RelAbsContext* ctx) const;

  std::vector<std::string> FallbackHeadVariables(const std::string& relation) const;

  std::unordered_map<std::string, std::string> BuildRenameMap(const std::vector<std::string>& from,
                                                              const std::vector<std::string>& to) const;

  void PrepareRecursiveBaseSafety();

  bool IsRecursiveContext(psr::BindingsFormulaContext* ctx) const;

  bool IsRecursiveCall(psr::FullApplContext* ctx) const;

  BoundSet RenameSafety(const BoundSet& safety, const std::string& relation,
                        const std::vector<std::string>& actual_variables);

  const std::vector<std::string>& HeadVariablesFor(const std::string& relation);

  void ComputeFullApplicationOnIDSafety(psr::FullApplContext* ctx, const std::string& id);

  std::unordered_map<std::string, std::vector<std::string>> relation_head_variables_;
  std::string current_relation_;
  std::optional<RecursionInfo> current_recursion_info_;
  BoundSet current_relation_base_safety_;
  bool has_current_relation_base_safety_ = false;
  std::unordered_set<std::shared_ptr<RelASTNode>> current_recursive_call_nodes_;
};

}  // namespace rel2sql

#endif  // SAFE_VISITOR_H
