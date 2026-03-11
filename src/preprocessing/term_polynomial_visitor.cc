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

void MarkInvalid(RelTerm* node) {
  node->term_linear_coeffs.reset();
  node->term_linear_invalid = true;
}

void SetLinear(RelTerm* node, LinearTermCoeffs coeffs) {
  node->term_linear_coeffs = std::move(coeffs);
  node->term_linear_invalid = false;
}

void SetLinearConstant(RelTerm* node, double b) {
  SetLinear(node, LinearTermCoeffs{{}, b});
}

void SetLinearSingleVar(RelTerm* node, const std::string& var, double a, double b) {
  LinearTermCoeffs c;
  c.var_coeffs[var] = a;
  c.constant = b;
  SetLinear(node, std::move(c));
}

bool HasValidLinear(RelTerm* node) {
  return !node->term_linear_invalid && node->term_linear_coeffs.has_value();
}

LinearTermCoeffs Add(const LinearTermCoeffs& lhs, const LinearTermCoeffs& rhs) {
  LinearTermCoeffs result;
  result.constant = lhs.constant + rhs.constant;
  for (const auto& [var, c] : lhs.var_coeffs) result.var_coeffs[var] = c;
  for (const auto& [var, c] : rhs.var_coeffs) {
    result.var_coeffs[var] += c;
  }
  return result;
}

LinearTermCoeffs Sub(const LinearTermCoeffs& lhs, const LinearTermCoeffs& rhs) {
  LinearTermCoeffs result;
  result.constant = lhs.constant - rhs.constant;
  for (const auto& [var, c] : lhs.var_coeffs) result.var_coeffs[var] = c;
  for (const auto& [var, c] : rhs.var_coeffs) {
    result.var_coeffs[var] -= c;
  }
  return result;
}

LinearTermCoeffs MulByConstant(const LinearTermCoeffs& c, double k) {
  LinearTermCoeffs result;
  result.constant = c.constant * k;
  for (const auto& [var, coeff] : c.var_coeffs) {
    result.var_coeffs[var] = coeff * k;
  }
  return result;
}

}  // namespace

std::shared_ptr<RelTerm> TermPolynomialVisitor::Visit(const std::shared_ptr<RelNumTerm>& node) {
  auto maybe_value = ConstantToDouble(node->value);
  if (!maybe_value.has_value()) {
    MarkInvalid(node.get());
    return node;
  }
  SetLinearConstant(node.get(), maybe_value.value());
  return node;
}

std::shared_ptr<RelTerm> TermPolynomialVisitor::Visit(const std::shared_ptr<RelIDTerm>& node) {
  if (node->variables.size() == 1) {
    SetLinearSingleVar(node.get(), *node->variables.begin(), 1.0, 0.0);
  } else if (node->variables.empty()) {
    // Identifier with no variables (e.g. constant) - treat as invalid for now
    MarkInvalid(node.get());
  } else {
    // Multiple variables in ID term - shouldn't happen for simple "x", but handle it
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

  const auto& c1 = node->lhs->term_linear_coeffs.value();
  const auto& c2 = node->rhs->term_linear_coeffs.value();

  switch (node->op) {
    case RelTermOp::ADD:
      SetLinear(node.get(), Add(c1, c2));
      return node;
    case RelTermOp::SUB:
      SetLinear(node.get(), Sub(c1, c2));
      return node;
    case RelTermOp::MUL: {
      bool lhs_has_var = !c1.var_coeffs.empty();
      bool rhs_has_var = !c2.var_coeffs.empty();

      if (lhs_has_var && rhs_has_var) {
        MarkInvalid(node.get());
        return node;
      }
      if (!lhs_has_var && !rhs_has_var) {
        SetLinearConstant(node.get(), c1.constant * c2.constant);
        return node;
      }
      if (lhs_has_var) {
        SetLinear(node.get(), MulByConstant(c1, c2.constant));
      } else {
        SetLinear(node.get(), MulByConstant(c2, c1.constant));
      }
      return node;
    }
    case RelTermOp::DIV: {
      if (!c2.var_coeffs.empty() || c2.constant == 0.0) {
        MarkInvalid(node.get());
        return node;
      }
      double inv = 1.0 / c2.constant;
      SetLinear(node.get(), MulByConstant(c1, inv));
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

}  // namespace rel2sql
