#include "preprocessing/term_polynomial_visitor.h"

#include <variant>

namespace rel2sql {

namespace {

// Helper to extract a numeric constant as double from sql::ast::constant_t.
std::optional<double> ConstantToDouble(const sql::ast::constant_t& c) {
  if (std::holds_alternative<int>(c)) {
    return static_cast<double>(std::get<int>(c));
  }
  if (std::holds_alternative<double>(c)) {
    return std::get<double>(c);
  }
  // Non-numeric constants (string, bool, etc.) are not supported.
  return std::nullopt;
}

// Convenience helpers to read/write linear metadata on nodes.
void MarkInvalid(const std::shared_ptr<RelASTNode>& node) {
  node->term_linear_coeffs.reset();
  node->term_linear_invalid = true;
}

void SetLinear(const std::shared_ptr<RelASTNode>& node, double a, double b) {
  node->term_linear_coeffs = std::make_pair(a, b);
  node->term_linear_invalid = false;
}

// Returns true if the node has a valid linear model.
bool HasValidLinear(const std::shared_ptr<RelASTNode>& node) {
  return !node->term_linear_invalid && node->term_linear_coeffs.has_value();
}

}  // namespace

TermPolynomialVisitor::TermPolynomialVisitor(std::shared_ptr<RelAST> ast) : BaseVisitor(std::move(ast)) {}

std::any TermPolynomialVisitor::visitNumTerm(psr::NumTermContext* ctx) {
  // LiteralVisitor has already populated the constant on the numericalConstant child.
  auto node = GetNode(ctx);
  auto num_const_node = GetNode(ctx->numericalConstant());

  if (!num_const_node->constant.has_value()) {
    MarkInvalid(node);
    return {};
  }

  auto maybe_value = ConstantToDouble(num_const_node->constant.value());
  if (!maybe_value.has_value()) {
    MarkInvalid(node);
    return {};
  }

  SetLinear(node, 0.0, maybe_value.value());
  return {};
}

std::any TermPolynomialVisitor::visitIDTerm(psr::IDTermContext* ctx) {
  auto node = GetNode(ctx);

  // VariablesVisitor has already populated variables/free_variables.
  if (node->variables.size() == 1) {
    // Single variable x -> a = 1, b = 0.
    SetLinear(node, 1.0, 0.0);
  } else {
    // Not a single variable (could be relation name, or unresolved id).
    MarkInvalid(node);
  }

  return {};
}

std::any TermPolynomialVisitor::visitOpTerm(psr::OpTermContext* ctx) {
  auto node = GetNode(ctx);

  // Visit children first to compute their linear models.
  visit(ctx->lhs);
  visit(ctx->rhs);

  auto lhs_node = GetNode(ctx->lhs);
  auto rhs_node = GetNode(ctx->rhs);

  // Enforce single-variable semantics at this subtree.
  if (node->variables.size() > 1) {
    MarkInvalid(node);
    return {};
  }

  // Both sides must have valid linear models.
  if (!HasValidLinear(lhs_node) || !HasValidLinear(rhs_node)) {
    MarkInvalid(node);
    return {};
  }

  auto [a1, b1] = lhs_node->term_linear_coeffs.value();
  auto [a2, b2] = rhs_node->term_linear_coeffs.value();

  const std::string op_text = ctx->arithmeticOperator()->getText();

  if (op_text == "+") {
    SetLinear(node, a1 + a2, b1 + b2);
    return {};
  }

  if (op_text == "-") {
    SetLinear(node, a1 - a2, b1 - b2);
    return {};
  }

  if (op_text == "*") {
    // Allow only when at most one operand depends on the variable; the other must be constant.
    bool lhs_has_var = !lhs_node->variables.empty();
    bool rhs_has_var = !rhs_node->variables.empty();

    if (lhs_has_var && rhs_has_var) {
      // Would yield degree >= 2.
      MarkInvalid(node);
      return {};
    }

    if (!lhs_has_var && !rhs_has_var) {
      // constant * constant
      SetLinear(node, 0.0, b1 * b2);
      return {};
    }

    // Exactly one side has the variable.
    if (lhs_has_var) {
      // (a1 x + b1) * b2, with rhs constant (a2 == 0).
      SetLinear(node, a1 * b2, b1 * b2);
    } else {
      // b1 * (a2 x + b2), with lhs constant (a1 == 0).
      SetLinear(node, a2 * b1, b2 * b1);
    }
    return {};
  }

  if (op_text == "/") {
    // Only allow division by a constant denominator.
    bool rhs_has_var = !rhs_node->variables.empty();
    if (rhs_has_var) {
      MarkInvalid(node);
      return {};
    }

    // rhs is constant: model must be (0 * x + b2)
    if (a2 != 0.0) {
      // Denominator depends on x syntactically.
      MarkInvalid(node);
      return {};
    }

    if (b2 == 0.0) {
      // Division by zero is invalid for our purposes.
      MarkInvalid(node);
      return {};
    }

    double inv = 1.0 / b2;
    SetLinear(node, a1 * inv, b1 * inv);
    return {};
  }

  // Unknown operator.
  MarkInvalid(node);
  return {};
}

std::any TermPolynomialVisitor::visitParenthesisTerm(psr::ParenthesisTermContext* ctx) {
  // Delegate to inner term and propagate its linear model.
  visit(ctx->term());

  auto node = GetNode(ctx);
  auto inner_node = GetNode(ctx->term());

  node->term_linear_invalid = inner_node->term_linear_invalid;
  node->term_linear_coeffs = inner_node->term_linear_coeffs;

  return {};
}

std::any TermPolynomialVisitor::visitTermExpr(psr::TermExprContext* ctx) {
  // Analyze the underlying term and mirror the result at the expression node.
  visit(ctx->term());

  auto node = GetNode(ctx);
  auto term_node = GetNode(ctx->term());

  node->term_linear_invalid = term_node->term_linear_invalid;
  node->term_linear_coeffs = term_node->term_linear_coeffs;

  return {};
}

}  // namespace rel2sql
