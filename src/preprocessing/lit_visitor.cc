#include "preprocessing/lit_visitor.h"

namespace rel2sql {

std::shared_ptr<RelDef> LiteralVisitor::Visit(const std::shared_ptr<RelDef>& node) {
  if (!node->body) return node;

  node->has_only_literal_values = true;
  node->body->has_only_literal_values = true;

  for (auto& expr : node->body->exprs) {
    if (expr) {
      Visit(expr);
      if (!expr->has_only_literal_values) {
        node->has_only_literal_values = false;
        node->body->has_only_literal_values = false;
      }
    }
  }

  if (!node->has_only_literal_values) {
    for (auto& expr : node->body->exprs) {
      if (expr) expr->has_only_literal_values = false;
    }
  }

  return node;
}

std::shared_ptr<RelExpr> LiteralVisitor::Visit(const std::shared_ptr<RelLiteral>& node) {
  node->constant = node->value;
  return node;
}

std::shared_ptr<RelExpr> LiteralVisitor::Visit(const std::shared_ptr<RelProduct>& node) {
  bool all_literal = true;
  for (auto& expr : node->exprs) {
    if (expr) {
      Visit(expr);
      if (!expr->constant.has_value()) all_literal = false;
    }
  }
  node->has_only_literal_values = all_literal;
  return node;
}

std::shared_ptr<RelTerm> LiteralVisitor::Visit(const std::shared_ptr<RelNumTerm>& node) {
  node->constant = node->value;
  return node;
}

}  // namespace rel2sql
