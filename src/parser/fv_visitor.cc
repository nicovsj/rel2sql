#include "fv_visitor.h"

std::any ExtendedASTVisitor::visitRelDef(rel_parser::PrunedCoreRelParser::RelDefContext *ctx) {
  auto result = std::any_cast<std::set<std::string>>(visitChildren(ctx));

  data_[ctx].free_variables = result;

  return result;
}

std::any ExtendedASTVisitor::visitRelAbs(rel_parser::PrunedCoreRelParser::RelAbsContext *ctx) {
  std::set<std::string> results;

  for (auto &child : ctx->expr()) {
    auto result = std::any_cast<std::set<std::string>>(visit(child));

    std::set_union(results.begin(), results.end(), result.begin(), result.end(), std::inserter(results, results.end()));
  }

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitIDExpr(rel_parser::PrunedCoreRelParser::IDExprContext *ctx) {
  std::set<std::string> results;

  results.insert(ctx->T_ID()->getText());

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitProductExpr(rel_parser::PrunedCoreRelParser::ProductExprContext *ctx) {
  std::set<std::string> results;

  for (auto &child : ctx->productInner()->expr()) {
    auto result = std::any_cast<std::set<std::string>>(visit(child));

    std::set_union(results.begin(), results.end(), result.begin(), result.end(), std::inserter(results, results.end()));
  }

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitConditionExpr(rel_parser::PrunedCoreRelParser::ConditionExprContext *ctx) {
  std::set<std::string> results;

  auto lh_result = std::any_cast<std::set<std::string>>(visit(ctx->lhs));

  auto rh_result = std::any_cast<std::set<std::string>>(visit(ctx->rhs));

  std::set_union(lh_result.begin(), lh_result.end(), rh_result.begin(), rh_result.end(),
                 std::inserter(results, results.end()));

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitRelAbsExpr(rel_parser::PrunedCoreRelParser::RelAbsExprContext *ctx) {
  auto result = std::any_cast<std::set<std::string>>(visit(ctx->relAbs()));

  data_[ctx].free_variables = result;

  return result;
}

std::any ExtendedASTVisitor::visitFormulaExpr(rel_parser::PrunedCoreRelParser::FormulaExprContext *ctx) {
  auto result = std::any_cast<std::set<std::string>>(visit(ctx->formula()));

  data_[ctx].free_variables = result;

  return result;
}

std::any ExtendedASTVisitor::visitBindingsExpr(rel_parser::PrunedCoreRelParser::BindingsExprContext *ctx) {
  std::set<std::string> results;

  auto bindings_result = std::any_cast<std::set<std::string>>(visit(ctx->bindingInner()));

  auto expr_result = std::any_cast<std::set<std::string>>(visit(ctx->expr()));

  std::set_difference(expr_result.begin(), expr_result.end(), bindings_result.begin(), bindings_result.end(),
                      std::inserter(results, results.end()));

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitBindingsFormula(rel_parser::PrunedCoreRelParser::BindingsFormulaContext *ctx) {
  std::set<std::string> results;

  auto bindings_result = std::any_cast<std::set<std::string>>(visit(ctx->bindingInner()));

  auto formula_result = std::any_cast<std::set<std::string>>(visit(ctx->formula()));

  std::set_difference(formula_result.begin(), formula_result.end(), bindings_result.begin(), bindings_result.end(),
                      std::inserter(results, results.end()));

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitPartialAppl(rel_parser::PrunedCoreRelParser::PartialApplContext *ctx) {
  std::set<std::string> results;

  auto base_result = std::any_cast<std::set<std::string>>(visit(ctx->applBase()));

  std::set_union(results.begin(), results.end(), base_result.begin(), base_result.end(),
                 std::inserter(results, results.end()));

  auto params_result = std::any_cast<std::set<std::string>>(visit(ctx->applParams()));

  std::set_union(results.begin(), results.end(), params_result.begin(), params_result.end(),
                 std::inserter(results, results.end()));

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitFullAppl(rel_parser::PrunedCoreRelParser::FullApplContext *ctx) {
  std::set<std::string> results;

  auto base_result = std::any_cast<std::set<std::string>>(visit(ctx->applBase()));

  std::set_union(results.begin(), results.end(), base_result.begin(), base_result.end(),
                 std::inserter(results, results.end()));

  auto params_result = std::any_cast<std::set<std::string>>(visit(ctx->applParams()));

  std::set_union(results.begin(), results.end(), params_result.begin(), params_result.end(),
                 std::inserter(results, results.end()));

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitBinOp(rel_parser::PrunedCoreRelParser::BinOpContext *ctx) {
  std::set<std::string> results;

  auto lh_result = std::any_cast<std::set<std::string>>(visit(ctx->lhs));

  auto rh_result = std::any_cast<std::set<std::string>>(visit(ctx->rhs));

  std::set_union(lh_result.begin(), lh_result.end(), rh_result.begin(), rh_result.end(),
                 std::inserter(results, results.end()));

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitUnOp(rel_parser::PrunedCoreRelParser::UnOpContext *ctx) {
  auto result = std::any_cast<std::set<std::string>>(visit(ctx->formula()));

  data_[ctx].free_variables = result;

  return result;
}

std::any ExtendedASTVisitor::visitQuantification(rel_parser::PrunedCoreRelParser::QuantificationContext *ctx) {
  std::set<std::string> results;

  auto formula_result = std::any_cast<std::set<std::string>>(visit(ctx->formula()));

  auto bindings_result = std::any_cast<std::set<std::string>>(visit(ctx->bindingInner()));

  std::set_difference(formula_result.begin(), formula_result.end(), bindings_result.begin(), bindings_result.end(),
                      std::inserter(results, results.end()));

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitParen(rel_parser::PrunedCoreRelParser::ParenContext *ctx) {
  auto result = std::any_cast<std::set<std::string>>(visit(ctx->formula()));

  data_[ctx].free_variables = result;

  return result;
}

std::any ExtendedASTVisitor::visitBindingInner(rel_parser::PrunedCoreRelParser::BindingInnerContext *ctx) {
  std::set<std::string> results;

  for (auto &child : ctx->binding()) {
    auto result = std::any_cast<std::set<std::string>>(visit(child));

    std::set_union(results.begin(), results.end(), result.begin(), result.end(), std::inserter(results, results.end()));
  }

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitBinding(rel_parser::PrunedCoreRelParser::BindingContext *ctx) {
  std::set<std::string> results;

  if (ctx->id) {
    results.insert(ctx->id->getText());
  }

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitApplBase(rel_parser::PrunedCoreRelParser::ApplBaseContext *ctx) {
  std::set<std::string> result;

  if (ctx->relAbs()) {
    result = std::any_cast<std::set<std::string>>(visit(ctx->relAbs()));
  }

  // Do nothing in the other case because the ID is not a variable

  data_[ctx].free_variables = result;

  return result;
}

std::any ExtendedASTVisitor::visitApplParams(rel_parser::PrunedCoreRelParser::ApplParamsContext *ctx) {
  std::set<std::string> results;

  for (auto &child : ctx->applParam()) {
    auto result = std::any_cast<std::set<std::string>>(visit(child));

    std::set_union(results.begin(), results.end(), result.begin(), result.end(), std::inserter(results, results.end()));
  }

  data_[ctx].free_variables = results;

  return results;
}

std::any ExtendedASTVisitor::visitApplParam(rel_parser::PrunedCoreRelParser::ApplParamContext *ctx) {
  std::set<std::string> result;

  if (ctx->expr()) {
    result = std::any_cast<std::set<std::string>>(visit(ctx->expr()));
  }

  data_[ctx].free_variables = result;

  return result;
}
