#include "preprocessing/term_polynomial_visitor.h"

#include <variant>

namespace rel2sql {

namespace {

std::optional<double> ConstantToDouble(const sql::ast::constant_t& c) {
  if (std::holds_alternative<int>(c)) {
    return static_cast<double>(std::get<int>(c));
  }
  if (std::holds_alternative<double>(c)) {
    return std::get<double>(c);
  }
  return std::nullopt;
}

void MarkInvalid(RelNode* node) {
  node->term_linear_coeffs.reset();
  node->term_linear_invalid = true;
}

void SetLinear(RelNode* node, double a, double b) {
  node->term_linear_coeffs = std::make_pair(a, b);
  node->term_linear_invalid = false;
}

bool HasValidLinear(RelNode* node) {
  return !node->term_linear_invalid && node->term_linear_coeffs.has_value();
}

}  // namespace

std::shared_ptr<RelTerm> TermPolynomialVisitor::Visit(const std::shared_ptr<RelNumTerm>& node) {
  auto maybe_value = ConstantToDouble(node->value);
  if (!maybe_value.has_value()) {
    MarkInvalid(node.get());
    return node;
  }
  SetLinear(node.get(), 0.0, maybe_value.value());
  return node;
}

std::shared_ptr<RelTerm> TermPolynomialVisitor::Visit(const std::shared_ptr<RelIDTerm>& node) {
  if (node->variables.size() == 1) {
    SetLinear(node.get(), 1.0, 0.0);
  } else {
    MarkInvalid(node.get());
  }
  return node;
}

std::shared_ptr<RelTerm> TermPolynomialVisitor::Visit(const std::shared_ptr<RelOpTerm>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);

  if (!node->lhs || !node->rhs || !HasValidLinear(node->lhs.get()) || !HasValidLinear(node->rhs.get())) {
    MarkInvalid(node.get());
    return node;
  }

  auto [a1, b1] = node->lhs->term_linear_coeffs.value();
  auto [a2, b2] = node->rhs->term_linear_coeffs.value();

  switch (node->op) {
    case RelTermOp::ADD:
      SetLinear(node.get(), a1 + a2, b1 + b2);
      return node;
    case RelTermOp::SUB:
      SetLinear(node.get(), a1 - a2, b1 - b2);
      return node;
    case RelTermOp::MUL: {
      bool lhs_has_var = !node->lhs->variables.empty();
      bool rhs_has_var = !node->rhs->variables.empty();

      if (lhs_has_var && rhs_has_var) {
        MarkInvalid(node.get());
        return node;
      }
      if (!lhs_has_var && !rhs_has_var) {
        SetLinear(node.get(), 0.0, b1 * b2);
        return node;
      }
      if (lhs_has_var) {
        SetLinear(node.get(), a1 * b2, b1 * b2);
      } else {
        SetLinear(node.get(), a2 * b1, b2 * b1);
      }
      return node;
    }
    case RelTermOp::DIV: {
      bool rhs_has_var = !node->rhs->variables.empty();
      if (rhs_has_var || a2 != 0.0 || b2 == 0.0) {
        MarkInvalid(node.get());
        return node;
      }
      double inv = 1.0 / b2;
      SetLinear(node.get(), a1 * inv, b1 * inv);
      return node;
    }
  }
  return node;
}

std::shared_ptr<RelTerm> TermPolynomialVisitor::Visit(const std::shared_ptr<RelParenthesisTerm>& node) {
  if (node->term) Visit(node->term);
  if (node->term) {
    node->term_linear_invalid = node->term->term_linear_invalid;
    node->term_linear_coeffs = node->term->term_linear_coeffs;
  }
  return node;
}

std::shared_ptr<RelExpr> TermPolynomialVisitor::Visit(const std::shared_ptr<RelTermExpr>& node) {
  if (node->term) Visit(node->term);
  if (node->term) {
    node->term_linear_invalid = node->term->term_linear_invalid;
    node->term_linear_coeffs = node->term->term_linear_coeffs;
  }
  return node;
}

}  // namespace rel2sql
