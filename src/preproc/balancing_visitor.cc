#include "balancing_visitor.h"

#include "exceptions.h"

namespace rel2sql {

BalancingVisitor::BalancingVisitor(std::shared_ptr<ExtendedASTData> ast_data) : BaseVisitor(ast_data) {}

std::any BalancingVisitor::visitProgram(psr::ProgramContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitRelDef(psr::RelDefContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitRelAbs(psr::RelAbsContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitProductExpr(psr::ProductExprContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitConditionExpr(psr::ConditionExprContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitRelAbsExpr(psr::RelAbsExprContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitFormulaExpr(psr::FormulaExprContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitBindingsExpr(psr::BindingsExprContext* ctx) {
  visit(ctx->expr());
  return {};
}

std::any BalancingVisitor::visitBindingsFormula(psr::BindingsFormulaContext* ctx) {
  visit(ctx->formula());
  return {};
}

std::any BalancingVisitor::visitPartialAppl(psr::PartialApplContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitProductInner(psr::ProductInnerContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitFullAppl(psr::FullApplContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitBinOp(psr::BinOpContext* ctx) {
  if (ctx->K_and()) {
    auto& node = GetNode(ctx);

    // Collect and store comparator formulas and other formulas
    CollectComparatorFormulas(ctx, node.comparator_formulas, node.other_formulas);

    // Check that all free variables in comparator formulas are also in other formulas
    if (!node.comparator_formulas.empty()) {
      std::set<std::string> other_free_variables;

      for (auto other_formula : node.other_formulas) {
        other_free_variables.insert(GetNode(other_formula).free_variables.begin(),
                                    GetNode(other_formula).free_variables.end());
      }

      for (auto comparator_formula : node.comparator_formulas) {
        for (auto free_variable : GetNode(comparator_formula).free_variables) {
          if (other_free_variables.find(free_variable) == other_free_variables.end()) {
            SourceLocation location = GetSourceLocation(comparator_formula);
            throw VariableException(
                "Free variable '" + free_variable + "' is not part of a non-comparator formula in the same conjunction",
                location);
          }
        }
      }
    }

    // Recursively visit other formulas
    for (auto other_formula : node.other_formulas) {
      visit(other_formula);
    }
  }

  visitChildren(ctx);

  return {};
}

std::any BalancingVisitor::visitUnOp(psr::UnOpContext* ctx) {
  visit(ctx->formula());
  return {};
}

std::any BalancingVisitor::visitQuantification(psr::QuantificationContext* ctx) {
  visit(ctx->formula());
  return {};
}

std::any BalancingVisitor::visitParen(psr::ParenContext* ctx) {
  visit(ctx->formula());
  return {};
}

std::any BalancingVisitor::visitApplBase(psr::ApplBaseContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitApplParams(psr::ApplParamsContext* ctx) {
  visitChildren(ctx);
  return {};
}

std::any BalancingVisitor::visitApplParam(psr::ApplParamContext* ctx) {
  visitChildren(ctx);
  return {};
}

void BalancingVisitor::CollectComparatorFormulas(psr::FormulaContext* formula_ctx,
                                                 std::vector<antlr4::ParserRuleContext*>& comparator_formulas,
                                                 std::vector<antlr4::ParserRuleContext*>& other_formulas) {
  /**
   * @brief Recursively collects comparator formulas and other formulas from a given formula context.
   *
   * This function traverses the formula tree and categorizes each subformula into either
   * comparator formulas or other formulas. It handles three cases:
   * 1. Comparison contexts are added to comparator_formulas.
   * 2. Binary operations with 'and' operator are recursively processed.
   * 3. All other formula types are added to other_formulas.
   * This process guarantees that all subformulas of a single conjunction are categorized
   * correctly. The categorization is needed for the SQL special construct for translation of terms.
   *
   * @param formula_ctx The root formula context to process.
   * @param comparator_formulas Vector to store collected comparator formula contexts.
   * @param other_formulas Vector to store all other formula contexts.
   *
   * @note This function modifies the input vectors comparator_formulas and other_formulas.
   * @note The function assumes that ComparisonContext represents a comparator formula.
   */

  if (dynamic_cast<psr::ComparisonContext*>(formula_ctx)) {
    comparator_formulas.push_back(formula_ctx);
  } else if (auto bin_op_ctx = dynamic_cast<psr::BinOpContext*>(formula_ctx)) {
    if (bin_op_ctx->K_and()) {
      CollectComparatorFormulas(bin_op_ctx->lhs, comparator_formulas, other_formulas);
      CollectComparatorFormulas(bin_op_ctx->rhs, comparator_formulas, other_formulas);
    } else {
      other_formulas.push_back(formula_ctx);
    }
  } else {
    other_formulas.push_back(formula_ctx);
  }
}

}  // namespace rel2sql
