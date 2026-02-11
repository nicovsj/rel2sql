#include "preprocessing/balancing_visitor.h"

#include "support/exceptions.h"

namespace rel2sql {

namespace {

SourceLocation GetSourceLocationFromNode(RelNode* node) {
  if (!node || !node->ctx) return SourceLocation(0, 0);
  auto* ctx = node->ctx;
  int line = ctx->getStart() ? ctx->getStart()->getLine() : 0;
  int column = ctx->getStart() ? ctx->getStart()->getCharPositionInLine() : 0;
  std::string text_snippet = ctx->getText();
  if (text_snippet.length() > 100) text_snippet = text_snippet.substr(0, 97) + "...";
  return SourceLocation(line, column, text_snippet);
}

}  // namespace

void BalancingVisitor::Visit(RelProgram& node) {
  RelASTVisitor::Visit(node);
}

void BalancingVisitor::Visit(RelDef& node) {
  RelASTVisitor::Visit(node);
}

void BalancingVisitor::Visit(RelAbstraction& node) {
  RelASTVisitor::Visit(node);
}

void BalancingVisitor::Visit(RelProductExpr& node) {
  RelASTVisitor::Visit(node);
}

void BalancingVisitor::Visit(RelConditionExpr& node) {
  if (node.lhs) {
    node.lhs->Accept(*this);
    condition_lhs_free_vars_stack_.push_back(node.lhs->free_variables);
  }
  if (node.rhs) node.rhs->Accept(*this);
  if (node.lhs) condition_lhs_free_vars_stack_.pop_back();
}

void BalancingVisitor::Visit(RelAbstractionExpr& node) {
  RelASTVisitor::Visit(node);
}

void BalancingVisitor::Visit(RelFormulaExpr& node) {
  RelASTVisitor::Visit(node);
}

void BalancingVisitor::Visit(RelBindingsExpr& node) {
  if (node.expr) node.expr->Accept(*this);
}

void BalancingVisitor::Visit(RelBindingsFormula& node) {
  if (node.formula) node.formula->Accept(*this);
}

void BalancingVisitor::Visit(RelPartialAppl& node) {
  RelASTVisitor::Visit(node);
}

void BalancingVisitor::Visit(RelFullAppl& node) {
  RelASTVisitor::Visit(node);
}

void BalancingVisitor::Visit(RelBinOp& node) {
  if (node.op != RelLogicalOp::AND) {
    RelASTVisitor::Visit(node);
    return;
  }

  CollectComparatorConjuncts(node.lhs, node.comparator_conjuncts, node.non_comparator_conjuncts);
  CollectComparatorConjuncts(node.rhs, node.comparator_conjuncts, node.non_comparator_conjuncts);

  if (!node.comparator_conjuncts.empty() && node.non_comparator_conjuncts.empty() &&
      !condition_lhs_free_vars_stack_.empty()) {
    const auto& allowed = condition_lhs_free_vars_stack_.back();
    for (const auto& comp : node.comparator_conjuncts) {
      for (const auto& free_var : comp->free_variables) {
        if (!allowed.count(free_var)) {
          throw VariableException(
              "Free variable '" + free_var +
                  "' in a comparator formula is not part of the left-hand side of the same condition expression",
              GetSourceLocationFromNode(comp.get()));
        }
      }
    }
  } else {
    ValidateComparatorFreeVariables(node.comparator_conjuncts, node.non_comparator_conjuncts);
  }

  CollectNegatedConjuncts(node.lhs, node.negated_conjuncts, node.non_negated_conjuncts);
  CollectNegatedConjuncts(node.rhs, node.negated_conjuncts, node.non_negated_conjuncts);
  ValidateNegatedFreeVariables(node.negated_conjuncts, node.non_negated_conjuncts);

  for (const auto& conj : node.non_comparator_conjuncts) {
    if (auto* f = dynamic_cast<RelFormula*>(conj.get())) {
      f->Accept(*this);
    }
  }
}

void BalancingVisitor::Visit(RelUnOp& node) {
  if (node.formula) node.formula->Accept(*this);
}

void BalancingVisitor::Visit(RelQuantification& node) {
  if (node.formula) node.formula->Accept(*this);
}

void BalancingVisitor::Visit(RelParen& node) {
  if (node.formula) node.formula->Accept(*this);
}

void BalancingVisitor::CollectComparatorConjuncts(
    const std::shared_ptr<RelFormula>& formula,
    std::vector<std::shared_ptr<RelNode>>& comparator_conjuncts,
    std::vector<std::shared_ptr<RelNode>>& non_comparator_conjuncts) {
  if (!formula) return;
  if (dynamic_cast<RelComparison*>(formula.get())) {
    comparator_conjuncts.push_back(formula);
    return;
  }
  auto* bin = dynamic_cast<RelBinOp*>(formula.get());
  if (bin && bin->op == RelLogicalOp::AND) {
    CollectComparatorConjuncts(bin->lhs, comparator_conjuncts, non_comparator_conjuncts);
    CollectComparatorConjuncts(bin->rhs, comparator_conjuncts, non_comparator_conjuncts);
    return;
  }
  non_comparator_conjuncts.push_back(formula);
}

void BalancingVisitor::CollectNegatedConjuncts(
    const std::shared_ptr<RelFormula>& formula,
    std::vector<std::shared_ptr<RelNode>>& negated_conjuncts,
    std::vector<std::shared_ptr<RelNode>>& non_negated_conjuncts) {
  if (!formula) return;
  auto* unop = dynamic_cast<RelUnOp*>(formula.get());
  if (unop) {
    negated_conjuncts.push_back(formula);
    return;
  }
  auto* bin = dynamic_cast<RelBinOp*>(formula.get());
  if (bin && bin->op == RelLogicalOp::AND) {
    CollectNegatedConjuncts(bin->lhs, negated_conjuncts, non_negated_conjuncts);
    CollectNegatedConjuncts(bin->rhs, negated_conjuncts, non_negated_conjuncts);
    return;
  }
  non_negated_conjuncts.push_back(formula);
}

std::expected<void, std::pair<std::string, SourceLocation>>
BalancingVisitor::ValidateFreeVariables(
    const std::vector<std::shared_ptr<RelNode>>& checked_conjuncts,
    const std::vector<std::shared_ptr<RelNode>>& reference_conjuncts) {
  if (checked_conjuncts.empty()) return {};

  std::set<std::string> reference_free_variables;
  for (const auto& ref : reference_conjuncts) {
    reference_free_variables.insert(ref->free_variables.begin(), ref->free_variables.end());
  }

  for (const auto& checked : checked_conjuncts) {
    for (const auto& free_var : checked->free_variables) {
      if (!reference_free_variables.count(free_var)) {
        return std::unexpected(
            std::make_pair(free_var, GetSourceLocationFromNode(checked.get())));
      }
    }
  }
  return {};
}

bool BalancingVisitor::IsInferrableTermEquality(RelComparison* comp,
                                                const std::set<std::string>& fv_non_comparator) const {
  if (!comp || comp->op != RelCompOp::EQ) return false;
  if (!comp->lhs || !comp->rhs) return false;
  std::set<std::string> missing;
  for (const auto& var : comp->free_variables) {
    if (fv_non_comparator.count(var) == 0) missing.insert(var);
  }
  return missing.size() == 1;
}

void BalancingVisitor::ValidateComparatorFreeVariables(
    const std::vector<std::shared_ptr<RelNode>>& comparator_conjuncts,
    const std::vector<std::shared_ptr<RelNode>>& non_comparator_conjuncts) {
  std::set<std::string> fv_non_comparator;
  for (const auto& ref : non_comparator_conjuncts) {
    fv_non_comparator.insert(ref->free_variables.begin(), ref->free_variables.end());
  }

  std::vector<std::shared_ptr<RelNode>> to_validate;
  for (const auto& comp : comparator_conjuncts) {
    auto* comp_node = dynamic_cast<RelComparison*>(comp.get());
    if (comp_node && IsInferrableTermEquality(comp_node, fv_non_comparator)) {
      continue;
    }
    to_validate.push_back(comp);
  }

  auto result = ValidateFreeVariables(to_validate, non_comparator_conjuncts);
  if (!result) {
    throw VariableException(
        "Free variable '" + result.error().first +
            "' in a comparator formula is not part of a non-comparator formula in the same conjunction",
        result.error().second);
  }
}

void BalancingVisitor::ValidateNegatedFreeVariables(
    const std::vector<std::shared_ptr<RelNode>>& negated_conjuncts,
    const std::vector<std::shared_ptr<RelNode>>& non_negated_conjuncts) {
  auto result = ValidateFreeVariables(negated_conjuncts, non_negated_conjuncts);
  if (!result) {
    throw VariableException(
        "Free variable '" + result.error().first +
            "' in a negated formula is not part of a non-negated formula in the same conjunction",
        result.error().second);
  }
}

}  // namespace rel2sql
