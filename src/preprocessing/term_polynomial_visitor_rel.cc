#include "preprocessing/term_polynomial_visitor_rel.h"

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

void TermPolynomialVisitorRel::Visit(RelNumTerm& node) {
  auto maybe_value = ConstantToDouble(node.value);
  if (!maybe_value.has_value()) {
    MarkInvalid(&node);
    return;
  }
  SetLinear(&node, 0.0, maybe_value.value());
}

void TermPolynomialVisitorRel::Visit(RelIDTerm& node) {
  if (node.variables.size() == 1) {
    SetLinear(&node, 1.0, 0.0);
  } else {
    MarkInvalid(&node);
  }
}

void TermPolynomialVisitorRel::Visit(RelOpTerm& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);

  if (node.variables.size() > 1) {
    MarkInvalid(&node);
    return;
  }

  if (!node.lhs || !node.rhs || !HasValidLinear(node.lhs.get()) || !HasValidLinear(node.rhs.get())) {
    MarkInvalid(&node);
    return;
  }

  auto [a1, b1] = node.lhs->term_linear_coeffs.value();
  auto [a2, b2] = node.rhs->term_linear_coeffs.value();

  switch (node.op) {
    case RelTermOp::ADD:
      SetLinear(&node, a1 + a2, b1 + b2);
      return;
    case RelTermOp::SUB:
      SetLinear(&node, a1 - a2, b1 - b2);
      return;
    case RelTermOp::MUL: {
      bool lhs_has_var = !node.lhs->variables.empty();
      bool rhs_has_var = !node.rhs->variables.empty();

      if (lhs_has_var && rhs_has_var) {
        MarkInvalid(&node);
        return;
      }
      if (!lhs_has_var && !rhs_has_var) {
        SetLinear(&node, 0.0, b1 * b2);
        return;
      }
      if (lhs_has_var) {
        SetLinear(&node, a1 * b2, b1 * b2);
      } else {
        SetLinear(&node, a2 * b1, b2 * b1);
      }
      return;
    }
    case RelTermOp::DIV: {
      bool rhs_has_var = !node.rhs->variables.empty();
      if (rhs_has_var || a2 != 0.0 || b2 == 0.0) {
        MarkInvalid(&node);
        return;
      }
      double inv = 1.0 / b2;
      SetLinear(&node, a1 * inv, b1 * inv);
      return;
    }
  }
}

void TermPolynomialVisitorRel::Visit(RelParenthesisTerm& node) {
  if (node.term) node.term->Accept(*this);
  if (node.term) {
    node.term_linear_invalid = node.term->term_linear_invalid;
    node.term_linear_coeffs = node.term->term_linear_coeffs;
  }
}

void TermPolynomialVisitorRel::Visit(RelTermExpr& node) {
  if (node.term) node.term->Accept(*this);
  if (node.term) {
    node.term_linear_invalid = node.term->term_linear_invalid;
    node.term_linear_coeffs = node.term->term_linear_coeffs;
  }
}

}  // namespace rel2sql
