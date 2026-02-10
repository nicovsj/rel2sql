#include "preprocessing/lit_visitor_rel.h"

#include <variant>

namespace rel2sql {

void LiteralVisitorRel::Visit(RelProgram& node) {
  RelASTVisitor::Visit(node);
}

void LiteralVisitorRel::Visit(RelDef& node) {
  if (!node.body) return;
  node.has_only_literal_values = true;
  node.body->has_only_literal_values = true;
  for (auto& expr : node.body->exprs) {
    if (expr) {
      expr->Accept(*this);
      if (!expr->has_only_literal_values) {
        node.has_only_literal_values = false;
        node.body->has_only_literal_values = false;
      }
    }
  }
  if (!node.has_only_literal_values) {
    for (auto& expr : node.body->exprs) {
      if (expr) expr->has_only_literal_values = false;
    }
  }
}

void LiteralVisitorRel::Visit(RelAbstraction& node) {
  RelASTVisitor::Visit(node);
}

void LiteralVisitorRel::Visit(RelLiteral& node) {
  node.constant = node.value;
}

void LiteralVisitorRel::Visit(RelLitExpr& node) {
  if (node.literal) node.literal->Accept(*this);
  node.has_only_literal_values = true;
  if (node.literal) node.constant = node.literal->constant;
}

void LiteralVisitorRel::Visit(RelProductExpr& node) {
  bool all_literal = true;
  for (auto& expr : node.exprs) {
    if (expr) {
      expr->Accept(*this);
      if (!expr->constant.has_value()) all_literal = false;
    }
  }
  node.has_only_literal_values = all_literal;
}

void LiteralVisitorRel::Visit(RelAbstractionExpr& node) {
  if (node.rel_abs) {
    node.rel_abs->Accept(*this);
    node.has_only_literal_values = node.rel_abs->has_only_literal_values;
  }
}

void LiteralVisitorRel::Visit(RelNumTerm& node) {
  node.constant = node.value;
}

}  // namespace rel2sql
