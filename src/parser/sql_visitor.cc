#include "sql_visitor.h"

#include "sql.h"

std::any SQLVisitor::visitConjunction(rel_parser::PrunedCoreRelParser::BinOpContext *ctx) {
  /*
   * Generates an SQL query from the conjunction of the two formulas.
   */
  auto lhs_sql = std::any_cast<std::shared_ptr<SelectStatement>>(visit(ctx->lhs));
  auto rhs_sql = std::any_cast<std::shared_ptr<SelectStatement>>(visit(ctx->rhs));

  auto lhs_subquery = std::make_shared<Subquery>(lhs_sql, "T1");
  auto rhs_subquery = std::make_shared<Subquery>(rhs_sql, "T2");

  std::unordered_map<ParserRuleContext *, std::shared_ptr<Source>> input_map = {{ctx->lhs, lhs_subquery},
                                                                                {ctx->rhs, rhs_subquery}};

  auto condition = EqualitySS(input_map, *extended_data_);

  auto select_columns = VarListSS(input_map, *extended_data_);

  return std::make_shared<SelectStatement>(select_columns,
                                           std::vector<std::shared_ptr<Source>>{lhs_subquery, rhs_subquery}, condition);
}

std::any SQLVisitor::visitDisjunction(rel_parser::PrunedCoreRelParser::BinOpContext *ctx) {
  /*
   * Generates an SQL query from the disjunction of the two formulas.
   */
  auto lhs_sql = std::any_cast<std::shared_ptr<SelectStatement>>(visit(ctx->lhs));
  auto rhs_sql = std::any_cast<std::shared_ptr<SelectStatement>>(visit(ctx->rhs));

  auto lhs_subquery = std::make_shared<Subquery>(lhs_sql, "T1");
  auto rhs_subquery = std::make_shared<Subquery>(rhs_sql, "T2");

  auto lhs_cols = VarListSS(std::unordered_map<ParserRuleContext *, std::shared_ptr<Source>>{{ctx->lhs, lhs_subquery}},
                            *extended_data_);
  auto rhs_cols = VarListSS(std::unordered_map<ParserRuleContext *, std::shared_ptr<Source>>{{ctx->rhs, rhs_subquery}},
                            *extended_data_);

  auto lhs_select = std::make_shared<SelectStatement>(lhs_cols, std::vector<std::shared_ptr<Source>>{lhs_subquery});
  auto rhs_select = std::make_shared<SelectStatement>(rhs_cols, std::vector<std::shared_ptr<Source>>{rhs_subquery});

  return std::make_shared<Union>(lhs_select, rhs_select);
}

std::any SQLVisitor::visitBinOp(rel_parser::PrunedCoreRelParser::BinOpContext *ctx) {
  if (ctx->K_and()) {
    return visitConjunction(ctx);
  } else if (ctx->K_or()) {
    return visitDisjunction(ctx);
  } else {
    throw std::runtime_error("Unknown binary operation");
  }
}
