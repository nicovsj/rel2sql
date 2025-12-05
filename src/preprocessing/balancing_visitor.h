#ifndef BALANCING_VISITOR_H
#define BALANCING_VISITOR_H

#include <antlr4-runtime.h>

#include <expected>

#include "preprocessing/base_visitor.h"
#include "rel_ast/extended_ast.h"

namespace rel2sql {

class BalancingVisitor : public BaseVisitor {
  /*
   * Utility visitor that computes the literal values of the Rel program and
   * stores them in the LitExprContext nodes.
   */
 public:
  explicit BalancingVisitor(std::shared_ptr<RelAST> ast);

  std::any visitProgram(psr::ProgramContext* ctx) override;

  std::any visitRelDef(psr::RelDefContext* ctx) override;

  std::any visitRelAbs(psr::RelAbsContext* ctx) override;

  // Expression branches

  std::any visitProductExpr(psr::ProductExprContext* ctx) override;

  std::any visitConditionExpr(psr::ConditionExprContext* ctx) override;

  std::any visitRelAbsExpr(psr::RelAbsExprContext* ctx) override;

  std::any visitFormulaExpr(psr::FormulaExprContext* ctx) override;

  std::any visitBindingsExpr(psr::BindingsExprContext* ctx) override;

  std::any visitBindingsFormula(psr::BindingsFormulaContext* ctx) override;

  std::any visitPartialAppl(psr::PartialApplContext* ctx) override;

  std::any visitProductInner(psr::ProductInnerContext* ctx) override;

  //  Formula branches

  std::any visitFullAppl(psr::FullApplContext* ctx) override;

  std::any visitBinOp(psr::BinOpContext* ctx) override;

  std::any visitUnOp(psr::UnOpContext* ctx) override;

  std::any visitQuantification(psr::QuantificationContext* ctx) override;

  std::any visitParen(psr::ParenContext* ctx) override;

  std::any visitApplBase(psr::ApplBaseContext* ctx) override;

  std::any visitApplParams(psr::ApplParamsContext* ctx) override;

  std::any visitApplParam(psr::ApplParamContext* ctx) override;

 private:
  /*
   * Recursively collects comparator conjuncts and non-comparator conjuncts from a given formula context.
   *
   * This function traverses the formula tree and categorizes each subformula into either
   * comparator conjuncts or non-comparator conjuncts. It handles three cases:
   * 1. Comparison contexts are added to comparator_conjuncts.
   * 2. Binary operations with 'and' operator are recursively processed.
   * 3. All other formula types are added to non_comparator_conjuncts.
   * This process guarantees that all subformulas of a single conjunction are categorized
   * correctly. The categorization is needed for the SQL special construct for translation of terms.
   *
   * @param formula_ctx The root formula context to process.
   * @param comparator_conjuncts Vector to store collected comparator conjunct contexts.
   * @param non_comparator_conjuncts Vector to store all non-comparator conjunct contexts.
   *
   * @note This function modifies the input vectors comparator_conjuncts and non_comparator_conjuncts.
   * @note The function assumes that ComparisonContext represents a comparator formula.
   */
  void CollectComparatorConjuncts(psr::FormulaContext* formula_ctx,
                                  std::vector<antlr4::ParserRuleContext*>& comparator_conjuncts,
                                  std::vector<antlr4::ParserRuleContext*>& non_comparator_conjuncts);

  /*
   * Validates that all free variables in a set of conjuncts are also present in another set of conjuncts.
   *
   * This function checks that every free variable appearing in the checked_conjuncts is also
   * present in at least one reference_conjunct. Returns expected<void> on success, or an error
   * containing the variable name and location of the offending conjunct on failure.
   *
   * @param checked_conjuncts Vector of conjunct contexts to validate.
   * @param reference_conjuncts Vector of reference conjunct contexts used for validation.
   * @return std::expected<void, std::pair<std::string, SourceLocation>> - void on success, error pair on failure.
   */
  std::expected<void, std::pair<std::string, SourceLocation>> ValidateFreeVariables(
      const std::vector<antlr4::ParserRuleContext*>& checked_conjuncts,
      const std::vector<antlr4::ParserRuleContext*>& reference_conjuncts);

  /*
   * Validates that all free variables in comparator conjuncts are also present in non-comparator conjuncts.
   *
   * @param comparator_conjuncts Vector of comparator conjunct contexts to validate.
   * @param non_comparator_conjuncts Vector of non-comparator conjunct contexts used for validation.
   *
   * @throws VariableException if any free variable in comparator_conjuncts is not found in non_comparator_conjuncts.
   */
  void ValidateComparatorFreeVariables(const std::vector<antlr4::ParserRuleContext*>& comparator_conjuncts,
                                       const std::vector<antlr4::ParserRuleContext*>& non_comparator_conjuncts);

  /*
   * Validates that all free variables in negated conjuncts are also present in non-negated conjuncts.
   *
   * @param negated_conjuncts Vector of negated conjunct contexts to validate.
   * @param non_negated_conjuncts Vector of non-negated conjunct contexts used for validation.
   *
   * @throws VariableException if any free variable in negated_conjuncts is not found in non_negated_conjuncts.
   */
  void ValidateNegatedFreeVariables(const std::vector<antlr4::ParserRuleContext*>& negated_conjuncts,
                                    const std::vector<antlr4::ParserRuleContext*>& non_negated_conjuncts);

  /*
   * Recursively collects negated conjuncts and non-negated conjuncts from a given formula context.
   *
   * This function traverses the formula tree and categorizes each subformula into either
   * negated conjuncts or non-negated conjuncts. It handles three cases:
   * 1. Unary operations with 'not' operator are added to negated_conjuncts.
   * 2. Binary operations with 'and' operator are recursively processed.
   * 3. All other formula types are added to non_negated_conjuncts.
   * This process guarantees that all subformulas of a single conjunction are categorized
   * correctly.
   *
   * @param formula_ctx The root formula context to process.
   * @param negated_conjuncts Vector to store collected negated conjunct contexts.
   * @param non_negated_conjuncts Vector to store all non-negated conjunct contexts.
   *
   * @note This function modifies the input vectors negated_conjuncts and non_negated_conjuncts.
   */
  void CollectNegatedConjuncts(psr::FormulaContext* formula_ctx,
                               std::vector<antlr4::ParserRuleContext*>& negated_conjuncts,
                               std::vector<antlr4::ParserRuleContext*>& non_negated_conjuncts);
};

}  // namespace rel2sql

#endif  // BALANCING_VISITOR_H
