#include "tree_structure_visitor.h"

namespace rel2sql {

void TreeStructureVisitor::PopulateChildren(antlr4::ParserRuleContext* ctx) {
  if (!ctx) return;

  auto parent_node = GetNode(ctx);
  parent_node->children.clear();

  // Iterate through all children of the context
  for (auto* child : ctx->children) {
    // Only include ParserRuleContext children, not terminal nodes
    auto* child_ctx = dynamic_cast<antlr4::ParserRuleContext*>(child);
    if (child_ctx) {
      auto child_node = GetNode(child_ctx);
      parent_node->children.push_back(child_node);
    }
  }
}

std::any TreeStructureVisitor::visitProgram(psr::ProgramContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitRelDef(psr::RelDefContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitRelAbs(psr::RelAbsContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitBindingInner(psr::BindingInnerContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitBinding(psr::BindingContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitApplBase(psr::ApplBaseContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitApplParams(psr::ApplParamsContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitApplParam(psr::ApplParamContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitProductInner(psr::ProductInnerContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

// Expression types
std::any TreeStructureVisitor::visitLitExpr(psr::LitExprContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitIDExpr(psr::IDExprContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitProductExpr(psr::ProductExprContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitConditionExpr(psr::ConditionExprContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitRelAbsExpr(psr::RelAbsExprContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitFormulaExpr(psr::FormulaExprContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitBindingsExpr(psr::BindingsExprContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitBindingsFormula(psr::BindingsFormulaContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitPartialAppl(psr::PartialApplContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

// Formula types
std::any TreeStructureVisitor::visitFullAppl(psr::FullApplContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitBinOp(psr::BinOpContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitUnOp(psr::UnOpContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitQuantification(psr::QuantificationContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitParen(psr::ParenContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitComparison(psr::ComparisonContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

// Term types
std::any TreeStructureVisitor::visitIDTerm(psr::IDTermContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitNumTerm(psr::NumTermContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

std::any TreeStructureVisitor::visitOpTerm(psr::OpTermContext* ctx) {
  visitChildren(ctx);
  PopulateChildren(ctx);
  return {};
}

}  // namespace rel2sql
