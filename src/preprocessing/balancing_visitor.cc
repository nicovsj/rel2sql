#include "balancing_visitor.h"

#include "support/exceptions.h"

namespace rel2sql {

BalancingVisitor::BalancingVisitor(std::shared_ptr<RelAST> ast) : BaseVisitor(ast) {}

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
  // Allow special balancing rules for comparator-only conjunctions under the RHS of `where`.
  // Free variables are already computed by VariablesVisitor (runs before BalancingVisitor).
  visit(ctx->lhs);
  condition_lhs_free_vars_stack_.push_back(GetNode(ctx->lhs)->free_variables);
  visit(ctx->rhs);
  condition_lhs_free_vars_stack_.pop_back();
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
    auto node = GetNode(ctx);

    // Collect and store comparator conjuncts and non-comparator conjuncts
    CollectComparatorConjuncts(ctx, node->comparator_conjuncts, node->non_comparator_conjuncts);

    // Validate that all free variables in comparator conjuncts are also present in:
    // - non-comparator conjuncts (general case), OR
    // - the LHS of the surrounding condition expression (special case: RHS is comparator-only)
    if (!node->comparator_conjuncts.empty() && node->non_comparator_conjuncts.empty() &&
        !condition_lhs_free_vars_stack_.empty()) {
      const auto& allowed = condition_lhs_free_vars_stack_.back();
      for (auto* comparator_ctx : node->comparator_conjuncts) {
        for (const auto& free_var : GetNode(comparator_ctx)->free_variables) {
          if (allowed.find(free_var) == allowed.end()) {
            SourceLocation location = GetSourceLocation(comparator_ctx);
            throw VariableException(
                "Free variable '" + free_var +
                    "' in a comparator formula is not part of the left-hand side of the same condition expression",
                location);
          }
        }
      }
    } else {
      ValidateComparatorFreeVariables(node->comparator_conjuncts, node->non_comparator_conjuncts);
    }

    // Collect and store negated conjuncts and non-negated conjuncts
    CollectNegatedConjuncts(ctx, node->negated_conjuncts, node->non_negated_conjuncts);

    // Validate that all free variables in negated conjuncts are also present in non-negated conjuncts
    ValidateNegatedFreeVariables(node->negated_conjuncts, node->non_negated_conjuncts);

    for (auto non_comparator_conjunct : node->non_comparator_conjuncts) {
      visit(non_comparator_conjunct);
    }

    return {};
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

void BalancingVisitor::CollectComparatorConjuncts(psr::FormulaContext* formula_ctx,
                                                  std::vector<antlr4::ParserRuleContext*>& comparator_conjuncts,
                                                  std::vector<antlr4::ParserRuleContext*>& non_comparator_conjuncts) {
  if (dynamic_cast<psr::ComparisonContext*>(formula_ctx)) {
    comparator_conjuncts.push_back(formula_ctx);
    return;
  } else if (auto bin_op_ctx = dynamic_cast<psr::BinOpContext*>(formula_ctx)) {
    if (bin_op_ctx->K_and()) {
      CollectComparatorConjuncts(bin_op_ctx->lhs, comparator_conjuncts, non_comparator_conjuncts);
      CollectComparatorConjuncts(bin_op_ctx->rhs, comparator_conjuncts, non_comparator_conjuncts);
      return;
    }
  }

  non_comparator_conjuncts.push_back(formula_ctx);
}

std::expected<void, std::pair<std::string, SourceLocation>> BalancingVisitor::ValidateFreeVariables(
    const std::vector<antlr4::ParserRuleContext*>& checked_conjuncts,
    const std::vector<antlr4::ParserRuleContext*>& reference_conjuncts) {
  if (checked_conjuncts.empty()) {
    return {};
  }

  std::set<std::string> reference_free_variables;

  for (auto reference_conjunct : reference_conjuncts) {
    reference_free_variables.insert(GetNode(reference_conjunct)->free_variables.begin(),
                                    GetNode(reference_conjunct)->free_variables.end());
  }

  for (auto checked_conjunct : checked_conjuncts) {
    for (auto free_variable : GetNode(checked_conjunct)->free_variables) {
      if (reference_free_variables.find(free_variable) == reference_free_variables.end()) {
        SourceLocation location = GetSourceLocation(checked_conjunct);
        return std::unexpected(std::make_pair(free_variable, location));
      }
    }
  }

  return {};
}

void BalancingVisitor::ValidateComparatorFreeVariables(
    const std::vector<antlr4::ParserRuleContext*>& comparator_conjuncts,
    const std::vector<antlr4::ParserRuleContext*>& non_comparator_conjuncts) {
  auto result = ValidateFreeVariables(comparator_conjuncts, non_comparator_conjuncts);
  if (!result) {
    throw VariableException(
        "Free variable '" + result.error().first +
            "' in a comparator formula is not part of a non-comparator formula in the same conjunction",
        result.error().second);
  }
}

void BalancingVisitor::ValidateNegatedFreeVariables(
    const std::vector<antlr4::ParserRuleContext*>& negated_conjuncts,
    const std::vector<antlr4::ParserRuleContext*>& non_negated_conjuncts) {
  auto result = ValidateFreeVariables(negated_conjuncts, non_negated_conjuncts);
  if (!result) {
    throw VariableException("Free variable '" + result.error().first +
                                "' in a negated formula is not part of a non-negated formula in the same conjunction",
                            result.error().second);
  }
}

void BalancingVisitor::CollectNegatedConjuncts(psr::FormulaContext* formula_ctx,
                                               std::vector<antlr4::ParserRuleContext*>& negated_conjuncts,
                                               std::vector<antlr4::ParserRuleContext*>& non_negated_conjuncts) {
  if (auto un_op_ctx = dynamic_cast<psr::UnOpContext*>(formula_ctx)) {
    if (un_op_ctx->K_not()) {
      negated_conjuncts.push_back(formula_ctx);
      return;
    }
  }

  if (auto bin_op_ctx = dynamic_cast<psr::BinOpContext*>(formula_ctx)) {
    if (bin_op_ctx->K_and()) {
      CollectNegatedConjuncts(bin_op_ctx->lhs, negated_conjuncts, non_negated_conjuncts);
      CollectNegatedConjuncts(bin_op_ctx->rhs, negated_conjuncts, non_negated_conjuncts);
      return;
    }
  }

  non_negated_conjuncts.push_back(formula_ctx);
}

}  // namespace rel2sql
