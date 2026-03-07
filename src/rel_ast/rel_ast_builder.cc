#include "rel_ast/rel_ast_builder.h"

#include <algorithm>
#include <stdexcept>

namespace rel2sql {

namespace {

void SetCtx(RelNode* node, antlr4::ParserRuleContext* ctx) {
  if (node && ctx) {
    node->ctx = ctx;
  }
}

template <typename T>
std::shared_ptr<T> Cast(std::any& a) {
  return std::any_cast<std::shared_ptr<T>>(a);
}

}  // namespace

std::shared_ptr<RelProgram> RelASTBuilder::Build(antlr4::ParserRuleContext* tree) {
  auto* program_ctx = dynamic_cast<psr::ProgramContext*>(tree);
  if (!program_ctx) {
    throw std::runtime_error("RelASTBuilder::Build expects ProgramContext");
  }
  auto result = visitProgram(program_ctx);
  return std::any_cast<std::shared_ptr<RelProgram>>(result);
}

std::shared_ptr<RelFormula> RelASTBuilder::BuildFromFormula(antlr4::ParserRuleContext* tree) {
  auto* formula_ctx = dynamic_cast<psr::FormulaContext*>(tree);
  if (!formula_ctx) {
    throw std::runtime_error("RelASTBuilder::BuildFromFormula expects FormulaContext");
  }
  auto result = visit(formula_ctx);
  return std::any_cast<std::shared_ptr<RelFormula>>(result);
}

std::shared_ptr<RelExpr> RelASTBuilder::BuildFromExpr(antlr4::ParserRuleContext* tree) {
  auto* expr_ctx = dynamic_cast<psr::ExprContext*>(tree);
  if (!expr_ctx) {
    throw std::runtime_error("RelASTBuilder::BuildFromExpr expects ExprContext");
  }
  auto result = visit(expr_ctx);
  return std::any_cast<std::shared_ptr<RelExpr>>(result);
}

RelCompOp RelASTBuilder::ParseCompOp(const std::string& op) {
  if (op == "=" || op == "==") return RelCompOp::EQ;
  if (op == "!=" || op == "≠") return RelCompOp::NEQ;
  if (op == "<") return RelCompOp::LT;
  if (op == ">") return RelCompOp::GT;
  if (op == "<=" || op == "≤") return RelCompOp::LTE;
  if (op == ">=" || op == "≥") return RelCompOp::GTE;
  throw std::runtime_error("Unknown comparator: " + op);
}

RelTermOp RelASTBuilder::ParseTermOp(const std::string& op) {
  if (op == "+") return RelTermOp::ADD;
  if (op == "-") return RelTermOp::SUB;
  if (op == "*") return RelTermOp::MUL;
  if (op == "/") return RelTermOp::DIV;
  throw std::runtime_error("Unknown term op: " + op);
}

std::any RelASTBuilder::visitProgram(psr::ProgramContext* ctx) {
  std::vector<std::shared_ptr<RelDef>> defs;
  for (auto* def_ctx : ctx->relDef()) {
    auto result = visit(def_ctx);
    defs.push_back(Cast<RelDef>(result));
  }
  auto program = std::make_shared<RelProgram>(defs);
  SetCtx(program.get(), ctx);
  return program;
}

std::any RelASTBuilder::visitRelDef(psr::RelDefContext* ctx) {
  std::string name = ctx->name->getText();
  auto body_result = visit(ctx->relAbs());
  auto body = Cast<RelAbstraction>(body_result);
  auto def = std::make_shared<RelDef>(std::move(name), std::move(body));
  SetCtx(def.get(), ctx);
  return def;
}

std::any RelASTBuilder::visitRelAbs(psr::RelAbsContext* ctx) {
  std::vector<std::shared_ptr<RelExpr>> exprs;
  for (auto* expr_ctx : ctx->expr()) {
    auto result = visit(expr_ctx);
    exprs.push_back(Cast<RelExpr>(result));
  }
  auto rel_abs = std::make_shared<RelAbstraction>(exprs);
  SetCtx(rel_abs.get(), ctx);
  return rel_abs;
}

std::any RelASTBuilder::visitLitExpr(psr::LitExprContext* ctx) {
  auto lit_result = visit(ctx->literal());
  auto node = Cast<RelLiteral>(lit_result);
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelExpr>(node);
}

std::any RelASTBuilder::visitProductExpr(psr::ProductExprContext* ctx) {
  auto inner_result = visit(ctx->productInner());
  auto exprs = std::any_cast<std::vector<std::shared_ptr<RelExpr>>>(inner_result);
  auto node = std::make_shared<RelProductExpr>(std::move(exprs));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelExpr>(node);
}

std::any RelASTBuilder::visitProductInner(psr::ProductInnerContext* ctx) {
  std::vector<std::shared_ptr<RelExpr>> exprs;
  for (auto* expr_ctx : ctx->expr()) {
    auto result = visit(expr_ctx);
    exprs.push_back(Cast<RelExpr>(result));
  }
  return exprs;
}

std::any RelASTBuilder::visitTermExpr(psr::TermExprContext* ctx) {
  auto term_result = visit(ctx->term());
  auto term = Cast<RelTerm>(term_result);
  SetCtx(term.get(), ctx);
  return std::shared_ptr<RelExpr>(term);
}

std::any RelASTBuilder::visitConditionExpr(psr::ConditionExprContext* ctx) {
  auto lhs_result = visit(ctx->lhs);
  auto rhs_result = visit(ctx->rhs);
  auto lhs = Cast<RelExpr>(lhs_result);
  auto rhs = Cast<RelFormula>(rhs_result);
  auto node = std::make_shared<RelConditionExpr>(std::move(lhs), std::move(rhs));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelExpr>(node);
}

std::any RelASTBuilder::visitRelAbsExpr(psr::RelAbsExprContext* ctx) {
  auto rel_abs_result = visit(ctx->relAbs());
  auto rel_abs = Cast<RelAbstraction>(rel_abs_result);
  auto node = std::make_shared<RelAbstractionExpr>(std::move(rel_abs));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelExpr>(node);
}

std::any RelASTBuilder::visitFormulaExpr(psr::FormulaExprContext* ctx) {
  auto formula_result = visit(ctx->formula());
  auto formula = Cast<RelFormula>(formula_result);
  auto node = std::make_shared<RelFormulaExpr>(std::move(formula));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelExpr>(node);
}

std::any RelASTBuilder::visitBindingsExpr(psr::BindingsExprContext* ctx) {
  auto bindings_result = visit(ctx->bindingInner());
  auto bindings = std::any_cast<std::vector<std::shared_ptr<RelBinding>>>(bindings_result);
  auto expr_result = visit(ctx->expr());
  auto expr = Cast<RelExpr>(expr_result);
  auto node = std::make_shared<RelBindingsExpr>(std::move(bindings), std::move(expr));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelExpr>(node);
}

std::any RelASTBuilder::visitBindingsFormula(psr::BindingsFormulaContext* ctx) {
  auto bindings_result = visit(ctx->bindingInner());
  auto bindings = std::any_cast<std::vector<std::shared_ptr<RelBinding>>>(bindings_result);
  auto formula_result = visit(ctx->formula());
  auto formula = Cast<RelFormula>(formula_result);
  auto node = std::make_shared<RelBindingsFormula>(std::move(bindings), std::move(formula));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelExpr>(node);
}

std::any RelASTBuilder::visitPartialAppl(psr::PartialApplContext* ctx) {
  auto base_result = visit(ctx->applBase());
  auto base = std::any_cast<std::shared_ptr<RelApplBase>>(base_result);
  auto params_result = visit(ctx->applParams());
  auto params = std::any_cast<std::vector<std::shared_ptr<RelApplParam>>>(params_result);
  auto node = std::make_shared<RelPartialAppl>(std::move(base), std::move(params));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelExpr>(node);
}

std::any RelASTBuilder::visitFormulaBool(psr::FormulaBoolContext* ctx) {
  auto node = std::make_shared<RelFormulaBool>();
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelFormula>(node);
}

std::any RelASTBuilder::visitFullAppl(psr::FullApplContext* ctx) {
  auto base_result = visit(ctx->applBase());
  auto base = std::any_cast<std::shared_ptr<RelApplBase>>(base_result);
  std::vector<std::shared_ptr<RelApplParam>> params;
  if (ctx->applParams()) {
    auto params_result = visit(ctx->applParams());
    params = std::any_cast<std::vector<std::shared_ptr<RelApplParam>>>(params_result);
  }
  auto node = std::make_shared<RelFullAppl>(std::move(base), std::move(params));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelFormula>(node);
}

std::any RelASTBuilder::visitQuantification(psr::QuantificationContext* ctx) {
  RelQuantOp op = ctx->op->getText() == "exists" ? RelQuantOp::EXISTS : RelQuantOp::FORALL;
  auto bindings_result = visit(ctx->bindingInner());
  auto bindings = std::any_cast<std::vector<std::shared_ptr<RelBinding>>>(bindings_result);
  auto formula_result = visit(ctx->formula());
  auto formula = Cast<RelFormula>(formula_result);
  if (op == RelQuantOp::EXISTS) {
    auto node = std::make_shared<RelExistential>(std::move(bindings), std::move(formula));
    SetCtx(node.get(), ctx);
    return std::shared_ptr<RelFormula>(node);
  }
  if (op == RelQuantOp::FORALL) {
    auto node = std::make_shared<RelUniversal>(std::move(bindings), std::move(formula));
    SetCtx(node.get(), ctx);
    return std::shared_ptr<RelFormula>(node);
  }
  throw std::runtime_error("Unknown quantification operator: " + ctx->op->getText());
}

std::any RelASTBuilder::visitParen(psr::ParenContext* ctx) {
  auto formula_result = visit(ctx->formula());
  auto formula = Cast<RelFormula>(formula_result);
  auto node = std::make_shared<RelParen>(std::move(formula));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelFormula>(node);
}

std::any RelASTBuilder::visitComparison(psr::ComparisonContext* ctx) {
  auto lhs_result = visit(ctx->lhs);
  auto rhs_result = visit(ctx->rhs);
  auto lhs = Cast<RelTerm>(lhs_result);
  auto rhs = Cast<RelTerm>(rhs_result);
  std::string op_str = ctx->comparator()->getText();
  RelCompOp op = ParseCompOp(op_str);
  auto node = std::make_shared<RelComparison>(std::move(lhs), op, std::move(rhs));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelFormula>(node);
}

std::any RelASTBuilder::visitUnOp(psr::UnOpContext* ctx) {
  auto formula_result = visit(ctx->formula());
  auto formula = Cast<RelFormula>(formula_result);
  auto node = std::make_shared<RelNegation>(std::move(formula));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelFormula>(node);
}

std::any RelASTBuilder::visitBinOp(psr::BinOpContext* ctx) {
  auto lhs_result = visit(ctx->lhs);
  auto rhs_result = visit(ctx->rhs);
  auto lhs = Cast<RelFormula>(lhs_result);
  auto rhs = Cast<RelFormula>(rhs_result);
  RelLogicalOp op = ctx->op->getText() == "and" ? RelLogicalOp::AND : RelLogicalOp::OR;
  if (op == RelLogicalOp::AND) {
    auto node = std::make_shared<RelConjunction>(std::move(lhs), std::move(rhs));
    SetCtx(node.get(), ctx);
    return std::shared_ptr<RelFormula>(node);
  }
  if (op == RelLogicalOp::OR) {
    auto node = std::make_shared<RelDisjunction>(std::move(lhs), std::move(rhs));
    SetCtx(node.get(), ctx);
    return std::shared_ptr<RelFormula>(node);
  }
  throw std::runtime_error("Unknown binary operator: " + ctx->op->getText());
}

std::any RelASTBuilder::visitIDTerm(psr::IDTermContext* ctx) {
  std::string id = ctx->getText();
  auto node = std::make_shared<RelIDTerm>(std::move(id));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelTerm>(node);
}

std::any RelASTBuilder::visitNumTerm(psr::NumTermContext* ctx) {
  auto num_result = visit(ctx->numericalConstant());
  auto value = std::any_cast<sql::ast::constant_t>(num_result);
  auto node = std::make_shared<RelNumTerm>(std::move(value));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelTerm>(node);
}

std::any RelASTBuilder::visitOpTerm(psr::OpTermContext* ctx) {
  auto lhs_result = visit(ctx->lhs);
  auto rhs_result = visit(ctx->rhs);
  auto lhs = Cast<RelTerm>(lhs_result);
  auto rhs = Cast<RelTerm>(rhs_result);
  std::string op_str = ctx->op->getText();
  RelTermOp op = ParseTermOp(op_str);
  auto node = std::make_shared<RelOpTerm>(std::move(lhs), op, std::move(rhs));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelTerm>(node);
}

std::any RelASTBuilder::visitParenthesisTerm(psr::ParenthesisTermContext* ctx) {
  auto term_result = visit(ctx->term());
  auto term = Cast<RelTerm>(term_result);
  auto node = std::make_shared<RelParenthesisTerm>(std::move(term));
  SetCtx(node.get(), ctx);
  return std::shared_ptr<RelTerm>(node);
}

std::any RelASTBuilder::visitInt(psr::IntContext* ctx) {
  int value = std::stoi(ctx->getText());
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(value));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitNegInt(psr::NegIntContext* ctx) {
  int value = std::stoi(ctx->getText());
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(value));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitFloat(psr::FloatContext* ctx) {
  double value = std::stod(ctx->getText());
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(value));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitNegFloat(psr::NegFloatContext* ctx) {
  double value = std::stod(ctx->getText());
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(value));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitChar(psr::CharContext* ctx) {
  std::string text = ctx->getText();
  text.erase(std::remove(text.begin(), text.end(), '\''), text.end());
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(std::move(text)));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitStr(psr::StrContext* ctx) {
  std::string text = ctx->getText();
  text.erase(std::remove(text.begin(), text.end(), '"'), text.end());
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(std::move(text)));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitBool(psr::BoolContext* ctx) {
  bool value = ctx->getText() == "true";
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(value));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitNumInt(psr::NumIntContext* ctx) {
  int value = std::stoi(ctx->getText());
  return sql::ast::constant_t(value);
}

std::any RelASTBuilder::visitNumNegInt(psr::NumNegIntContext* ctx) {
  int value = std::stoi(ctx->getText());
  return sql::ast::constant_t(value);
}

std::any RelASTBuilder::visitNumFloat(psr::NumFloatContext* ctx) {
  double value = std::stod(ctx->getText());
  return sql::ast::constant_t(value);
}

std::any RelASTBuilder::visitNumNegFloat(psr::NumNegFloatContext* ctx) {
  double value = std::stod(ctx->getText());
  return sql::ast::constant_t(value);
}

std::any RelASTBuilder::visitMetaInt(psr::MetaIntContext* ctx) {
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(ctx->getText()));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitRelName(psr::RelNameContext* ctx) {
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(ctx->getText()));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitRelNameStr(psr::RelNameStrContext* ctx) {
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(ctx->getText()));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitRelNameMstr(psr::RelNameMstrContext* ctx) {
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(ctx->getText()));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitMstr(psr::MstrContext* ctx) {
  std::string text = ctx->getText();
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(std::move(text)));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitRawstr(psr::RawstrContext* ctx) {
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(ctx->getText()));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitDate(psr::DateContext* ctx) {
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(ctx->getText()));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitDatetime(psr::DatetimeContext* ctx) {
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(ctx->getText()));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitInterpol(psr::InterpolContext* ctx) {
  auto node = std::make_shared<RelLiteral>(RelLiteralValue(ctx->getText()));
  SetCtx(node.get(), ctx);
  return node;
}

std::any RelASTBuilder::visitBindingInner(psr::BindingInnerContext* ctx) {
  std::vector<std::shared_ptr<RelBinding>> bindings;
  for (auto* binding_ctx : ctx->binding()) {
    auto result = visit(binding_ctx);
    bindings.push_back(std::any_cast<std::shared_ptr<RelBinding>>(result));
  }
  return bindings;
}

std::any RelASTBuilder::visitBinding(psr::BindingContext* ctx) {
  if (ctx->literal()) {
    auto lit_result = visit(ctx->literal());
    auto literal = Cast<RelLiteral>(lit_result);
    return std::shared_ptr<RelBinding>(std::make_shared<RelLiteralBinding>(literal->value));
  }
  std::string id = ctx->id->getText();
  std::optional<std::string> domain;
  if (ctx->id_domain) {
    domain = ctx->id_domain->getText();
  }
  return std::shared_ptr<RelBinding>(std::make_shared<RelVarBinding>(std::move(id), domain));
}

std::any RelASTBuilder::visitApplBase(psr::ApplBaseContext* ctx) {
  if (ctx->T_ID()) {
    return std::shared_ptr<RelApplBase>(std::make_shared<RelIDApplBase>(ctx->T_ID()->getText()));
  }
  auto rel_abs_result = visit(ctx->relAbs());
  auto rel_abs = Cast<RelAbstraction>(rel_abs_result);
  return std::shared_ptr<RelApplBase>(std::make_shared<RelAbstractionApplBase>(std::move(rel_abs)));
}

std::any RelASTBuilder::visitApplParams(psr::ApplParamsContext* ctx) {
  std::vector<std::shared_ptr<RelApplParam>> params;
  for (auto* param_ctx : ctx->applParam()) {
    auto result = visit(param_ctx);
    params.push_back(std::any_cast<std::shared_ptr<RelApplParam>>(result));
  }
  return params;
}

std::any RelASTBuilder::visitApplParam(psr::ApplParamContext* ctx) {
  if (ctx->underscore) {
    return std::shared_ptr<RelApplParam>(std::make_shared<RelUnderscoreParam>());
  }
  auto expr_result = visit(ctx->expr());
  auto expr = Cast<RelExpr>(expr_result);
  return std::shared_ptr<RelApplParam>(std::make_shared<RelExprApplParam>(std::move(expr)));
}

}  // namespace rel2sql
