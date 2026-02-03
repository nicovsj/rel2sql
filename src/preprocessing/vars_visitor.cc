#include "vars_visitor.h"

namespace rel2sql {

VariablesVisitor::VariablesVisitor(std::shared_ptr<RelAST> extended_ast) : BaseVisitor(extended_ast) {}

std::any VariablesVisitor::visitProgram(psr::ProgramContext* ctx) {
  auto node = GetNode(ctx);

  for (auto& child_ctx : ctx->relDef()) {
    visit(child_ctx);
    node->VariablesInplaceUnion(*GetNode(child_ctx));
  }

  return {};
}

std::any VariablesVisitor::visitRelDef(psr::RelDefContext* ctx) {
  visit(ctx->relAbs());

  auto child_node = GetNode(ctx->relAbs());
  auto node = GetNode(ctx);

  node->variables = child_node->variables;
  node->free_variables = child_node->free_variables;

  return {};
}

std::any VariablesVisitor::visitRelAbs(psr::RelAbsContext* ctx) {
  auto node = GetNode(ctx);

  for (auto& child_ctx : ctx->expr()) {
    visit(child_ctx);
    node->VariablesInplaceUnion(*GetNode(child_ctx));
  }

  return {};
}

std::any VariablesVisitor::visitIDTerm(psr::IDTermContext* ctx) {
  auto node = GetNode(ctx);

  std::string id = ctx->T_ID()->getText();

  if (ast_->IsVar(id)) {
    node->free_variables.insert(id);
    node->variables.insert(id);
  }

  return {};
}

std::any VariablesVisitor::visitOpTerm(psr::OpTermContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->lhs);
  visit(ctx->rhs);

  auto lhs_node = GetNode(ctx->lhs);
  auto rhs_node = GetNode(ctx->rhs);

  node->variables = lhs_node->variables;
  node->free_variables = lhs_node->free_variables;

  node->VariablesInplaceUnion(*rhs_node);

  return {};
}

std::any VariablesVisitor::visitParenthesisTerm(psr::ParenthesisTermContext* ctx) {
  visit(ctx->term());
  auto node = GetNode(ctx);
  auto term_node = GetNode(ctx->term());

  node->variables = term_node->variables;
  node->free_variables = term_node->free_variables;
  return {};
}

std::any VariablesVisitor::visitTermExpr(psr::TermExprContext* ctx) {
  visit(ctx->term());
  auto node = GetNode(ctx);
  auto term_node = GetNode(ctx->term());

  node->variables = term_node->variables;
  node->free_variables = term_node->free_variables;

  return {};
}

std::any VariablesVisitor::visitProductExpr(psr::ProductExprContext* ctx) {
  auto node = GetNode(ctx);

  for (auto& child : ctx->productInner()->expr()) {
    visit(child);
    node->VariablesInplaceUnion(*GetNode(child));
  }

  return {};
}

std::any VariablesVisitor::visitConditionExpr(psr::ConditionExprContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->lhs);
  visit(ctx->rhs);

  node->VariablesInplaceUnion(*GetNode(ctx->lhs));
  node->VariablesInplaceUnion(*GetNode(ctx->rhs));

  return {};
}

std::any VariablesVisitor::visitRelAbsExpr(psr::RelAbsExprContext* ctx) {
  visit(ctx->relAbs());

  auto child_node = GetNode(ctx->relAbs());

  auto node = GetNode(ctx);
  node->variables = child_node->variables;
  node->free_variables = child_node->free_variables;

  return {};
}

std::any VariablesVisitor::visitFormulaExpr(psr::FormulaExprContext* ctx) {
  visit(ctx->formula());

  auto child_node = GetNode(ctx->formula());

  auto node = GetNode(ctx);
  node->variables = child_node->variables;
  node->free_variables = child_node->free_variables;

  return {};
}

std::any VariablesVisitor::visitBindingsExpr(psr::BindingsExprContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->expr());
  visit(ctx->bindingInner());

  auto child_node = GetNode(ctx->expr());

  node->variables = child_node->variables;
  node->free_variables = child_node->free_variables;

  node->VariablesInplaceDifference(*GetNode(ctx->bindingInner()));

  return {};
}

std::any VariablesVisitor::visitBindingsFormula(psr::BindingsFormulaContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->formula());
  visit(ctx->bindingInner());

  auto child_node = GetNode(ctx->formula());

  node->variables = child_node->variables;
  node->free_variables = child_node->free_variables;

  node->VariablesInplaceDifference(*GetNode(ctx->bindingInner()));

  return {};
}

std::any VariablesVisitor::visitPartialAppl(psr::PartialApplContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->applBase());
  visit(ctx->applParams());

  auto base_node = GetNode(ctx->applBase());

  node->variables = base_node->variables;
  node->free_variables = base_node->free_variables;

  node->VariablesInplaceUnion(*GetNode(ctx->applParams()));

  return {};
}

std::any VariablesVisitor::visitFullAppl(psr::FullApplContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->applBase());
  visit(ctx->applParams());

  auto base_node = GetNode(ctx->applBase());

  node->variables = base_node->variables;
  node->free_variables = base_node->free_variables;

  node->VariablesInplaceUnion(*GetNode(ctx->applParams()));

  return {};
}

std::any VariablesVisitor::visitBinOp(psr::BinOpContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->lhs);
  visit(ctx->rhs);

  node->variables = GetNode(ctx->lhs)->variables;
  node->free_variables = GetNode(ctx->lhs)->free_variables;

  node->VariablesInplaceUnion(*GetNode(ctx->rhs));

  return {};
}

std::any VariablesVisitor::visitUnOp(psr::UnOpContext* ctx) {
  visit(ctx->formula());

  auto node = GetNode(ctx);
  node->variables = GetNode(ctx->formula())->variables;
  node->free_variables = GetNode(ctx->formula())->free_variables;

  return {};
}

std::any VariablesVisitor::visitQuantification(psr::QuantificationContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->formula());
  visit(ctx->bindingInner());

  node->variables = GetNode(ctx->formula())->variables;
  node->free_variables = GetNode(ctx->formula())->free_variables;

  node->VariablesInplaceDifference(*GetNode(ctx->bindingInner()));

  return {};
}

std::any VariablesVisitor::visitParen(psr::ParenContext* ctx) {
  visit(ctx->formula());

  auto node = GetNode(ctx);
  node->variables = GetNode(ctx->formula())->variables;
  node->free_variables = GetNode(ctx->formula())->free_variables;

  return {};
}

std::any VariablesVisitor::visitComparison(psr::ComparisonContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->lhs);
  visit(ctx->rhs);

  node->variables = GetNode(ctx->lhs)->variables;
  node->free_variables = GetNode(ctx->lhs)->free_variables;

  node->VariablesInplaceUnion(*GetNode(ctx->rhs));

  return {};
}

std::any VariablesVisitor::visitBindingInner(psr::BindingInnerContext* ctx) {
  auto node = GetNode(ctx);

  for (auto& child_ctx : ctx->binding()) {
    visit(child_ctx);
    node->VariablesInplaceUnion(*GetNode(child_ctx));
  }

  return {};
}

std::any VariablesVisitor::visitBinding(psr::BindingContext* ctx) {
  auto node = GetNode(ctx);

  if (ctx->id) {
    std::string id = ctx->id->getText();
    node->variables.insert(id);
    node->free_variables.insert(id);
  }

  return {};
}

std::any VariablesVisitor::visitApplBase(psr::ApplBaseContext* ctx) {
  auto node = GetNode(ctx);

  if (ctx->relAbs()) {
    visit(ctx->relAbs());
    node->variables = GetNode(ctx->relAbs())->variables;
    node->free_variables = GetNode(ctx->relAbs())->free_variables;
  }
  // Do nothing in the other case because the ID is not a variable

  return {};
}

std::any VariablesVisitor::visitApplParams(psr::ApplParamsContext* ctx) {
  auto node = GetNode(ctx);

  for (auto& child_ctx : ctx->applParam()) {
    visit(child_ctx);
    node->VariablesInplaceUnion(*GetNode(child_ctx));
  }

  return {};
}

std::any VariablesVisitor::visitApplParam(psr::ApplParamContext* ctx) {
  auto node = GetNode(ctx);

  if (ctx->expr()) {
    visit(ctx->expr());
    node->variables = GetNode(ctx->expr())->variables;
    node->free_variables = GetNode(ctx->expr())->free_variables;
  }

  return {};
}

}  // namespace rel2sql
