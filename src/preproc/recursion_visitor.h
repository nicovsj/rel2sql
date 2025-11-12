#ifndef RECURSION_VISITOR_H
#define RECURSION_VISITOR_H

#include <antlr4-runtime.h>

#include "preproc/base_visitor.h"
#include "structs/extended_ast.h"

namespace rel2sql {

class RecursionVisitor : public BaseVisitor {
  /*
   * Visitor that identifies Rel definitions matching the recursion pattern:
   * def Q {(B) : G or exists((u) | Q(w) and F(v))}
   * and sets the recursion flag on the corresponding node.
   */
 public:
  RecursionVisitor(std::shared_ptr<ExtendedASTData> extended_ast);

  std::any visitProgram(psr::ProgramContext* ctx) override;

  std::any visitRelDef(psr::RelDefContext* ctx) override;

  std::any visitRelAbs(psr::RelAbsContext* ctx) override;

  std::any visitBindingsFormula(psr::BindingsFormulaContext* ctx) override;

  std::any visitBinOp(psr::BinOpContext* ctx) override;

  std::any visitQuantification(psr::QuantificationContext* ctx) override;

  std::any visitFullAppl(psr::FullApplContext* ctx) override;

 private:
  // Current ID being defined (Q in the pattern)
  std::string current_q_;

  // Check if an ID is recursive (has cycle in dependency graph)
  bool IsRecursiveID(const std::string& id) const;

  // Collect all ID references (not variables) in a context
  std::unordered_set<std::string> CollectIDs(antlr4::ParserRuleContext* ctx) const;

  // Check if all IDs are EDBs or non-recursive IDBs (excluding current_q)
  bool OnlyEDBsOrNonRecursiveIDBs(const std::unordered_set<std::string>& ids,
                                   const std::string& current_q) const;

  // Check if variables are from binding or quantification binding
  bool VariablesFromBindingOrQuantification(
      const std::set<std::string>& vars, psr::BindingInnerContext* binding_ctx,
      psr::BindingInnerContext* quant_binding_ctx) const;

  // Check if a formula matches the pattern: G or exists(...) or exists(...)
  // Returns true if matches, and sets recursion flag on the node
  bool CheckRecursionPattern(psr::FormulaContext* formula_ctx,
                              psr::BindingInnerContext* binding_ctx);

  // Check if a formula is of the form: Q(w) and F(v)
  // Returns true if matches and F doesn't refer to Q
  bool CheckExistsPattern(psr::FormulaContext* formula_ctx, const std::string& q,
                          psr::BindingInnerContext* quant_binding_ctx,
                          psr::BindingInnerContext* outer_binding_ctx);

  // Check if a FullAppl is a call to Q
  bool IsCallToQ(psr::FullApplContext* ctx, const std::string& q) const;

  // Helper to collect all OR disjuncts in a formula
  void CollectOrDisjuncts(psr::FormulaContext* formula_ctx,
                          std::vector<psr::FormulaContext*>& disjuncts) const;

  // Helper to find Q(w) and F(v) parts in an AND formula
  void FindAndPatternParts(psr::FormulaContext* formula_ctx, const std::string& q,
                           psr::FullApplContext*& q_call, psr::FormulaContext*& f_part) const;
};

}  // namespace rel2sql

#endif  // RECURSION_VISITOR_H
