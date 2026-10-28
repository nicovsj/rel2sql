#include "rewriter/negated_comparison_rewriter.h"

#include "rel_ast/rel_ast.h"

namespace rel2sql {

namespace {

RelCompOp Opposite(RelCompOp op) {
  switch (op) {
    case RelCompOp::EQ:
      return RelCompOp::NEQ;
    case RelCompOp::NEQ:
      return RelCompOp::EQ;
    case RelCompOp::LT:
      return RelCompOp::GTE;
    case RelCompOp::GT:
      return RelCompOp::LTE;
    case RelCompOp::LTE:
      return RelCompOp::GT;
    case RelCompOp::GTE:
      return RelCompOp::LT;
  }
  return op;
}

}  // namespace

std::shared_ptr<RelFormula> NegatedComparisonRewriter::Visit(const std::shared_ptr<RelNegation>& node) {
  BaseRelVisitor::Visit(node);
  auto inner = node->formula;
  while (auto paren = std::dynamic_pointer_cast<RelParen>(inner)) inner = paren->formula;
  if (auto cmp = std::dynamic_pointer_cast<RelComparison>(inner)) {
    return std::make_shared<RelComparison>(cmp->lhs, Opposite(cmp->op), cmp->rhs);
  }
  return node;
}

}  // namespace rel2sql
