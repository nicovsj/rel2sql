#include "ids_visitor.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

#include "support/exceptions.h"

namespace rel2sql {

static const std::unordered_set<std::string> kSQLKeywords = {
    "SELECT", "DISTINCT", "FROM",      "WHERE", "GROUP",  "BY",    "WITH", "AS",  "UNION", "ALL",  "VALUES",
    "CASE",   "WHEN",     "THEN",      "END",   "EXISTS", "IN",    "NOT",  "AND", "OR",    "TRUE", "FALSE",
    "CREATE", "REPLACE",  "RECURSIVE", "VIEW",  "TABLE",  "COUNT", "SUM",  "AVG", "MIN",   "MAX",  "JOIN"};

static bool IsSQLKeyword(const std::string& identifier) {
  std::string normalized;
  normalized.reserve(identifier.size());
  for (unsigned char ch : identifier) {
    normalized.push_back(static_cast<char>(std::toupper(ch)));
  }
  return kSQLKeywords.find(normalized) != kSQLKeywords.end();
}

using StringSet = std::unordered_set<std::string>;

IDsVisitor::IDsVisitor(std::shared_ptr<RelAST> extended_ast) : BaseVisitor(extended_ast) {}

std::any IDsVisitor::visitProgram(psr::ProgramContext* ctx) {
  for (auto& child_ctx : ctx->relDef()) {
    visit(child_ctx);
  }

  ast_->RemoveVarsFromDependencyGraph();
  ast_->ComputeTopologicalSort();

  return {};
}

std::any IDsVisitor::visitRelDef(psr::RelDefContext* ctx) {
  std::string id = ctx->T_ID()->getText();

  if (IsSQLKeyword(id)) {
    throw SemanticException("Relation name '" + id + "' is a reserved SQL keyword", ErrorCode::RESERVED_RELATION_NAME,
                            GetSourceLocation(ctx));
  }

  ast_->MarkAsIDB(id);  // If defined in the program, it is an IDB (arity will be set later)

  auto deps = std::any_cast<StringSet>(visit(ctx->relAbs()));

  for (const auto& dep : deps) {
    ast_->AddDependency(id, dep);
  }

  return {};
}

std::any IDsVisitor::visitRelAbs(psr::RelAbsContext* ctx) {
  StringSet deps;

  for (auto& child_ctx : ctx->expr()) {
    auto child_deps = std::any_cast<StringSet>(visit(child_ctx));
    std::set_union(deps.begin(), deps.end(), child_deps.begin(), child_deps.end(), std::inserter(deps, deps.begin()));
  }

  return deps;
}

std::any IDsVisitor::visitIDTerm(psr::IDTermContext* ctx) {
  std::string id = ctx->T_ID()->getText();

  ast_->AddVar(id);

  return StringSet{id};
}

std::any IDsVisitor::visitNumTerm(psr::NumTermContext* _) { return StringSet{}; }

std::any IDsVisitor::visitOpTerm(psr::OpTermContext* ctx) {
  StringSet deps;

  auto lhs_deps = std::any_cast<StringSet>(visit(ctx->lhs));
  std::set_union(deps.begin(), deps.end(), lhs_deps.begin(), lhs_deps.end(), std::inserter(deps, deps.begin()));

  auto rhs_deps = std::any_cast<StringSet>(visit(ctx->rhs));
  std::set_union(deps.begin(), deps.end(), rhs_deps.begin(), rhs_deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitParenthesisTerm(psr::ParenthesisTermContext* ctx) {
  return visit(ctx->term());
}

std::any IDsVisitor::visitLitExpr(psr::LitExprContext* _) { return StringSet{}; }

std::any IDsVisitor::visitTermExpr(psr::TermExprContext* ctx) {
  // If the term contains an identifier, it will be handled by visitIDTerm
  return visit(ctx->term());
}

std::any IDsVisitor::visitProductExpr(psr::ProductExprContext* ctx) {
  StringSet deps;

  for (auto& child_ctx : ctx->productInner()->expr()) {
    auto child_deps = std::any_cast<StringSet>(visit(child_ctx));
    std::set_union(deps.begin(), deps.end(), child_deps.begin(), child_deps.end(), std::inserter(deps, deps.begin()));
  }

  return deps;
}

std::any IDsVisitor::visitConditionExpr(psr::ConditionExprContext* ctx) {
  StringSet deps;

  auto lhs_deps = std::any_cast<StringSet>(visit(ctx->expr()));
  std::set_union(deps.begin(), deps.end(), lhs_deps.begin(), lhs_deps.end(), std::inserter(deps, deps.begin()));

  auto rhs_deps = std::any_cast<StringSet>(visit(ctx->formula()));
  std::set_union(deps.begin(), deps.end(), rhs_deps.begin(), rhs_deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitRelAbsExpr(psr::RelAbsExprContext* ctx) {
  return visit(ctx->relAbs());
  ;
}

std::any IDsVisitor::visitFormulaExpr(psr::FormulaExprContext* ctx) { return visit(ctx->formula()); }

// FIXME: This is not correct, we need to visit the bindings
std::any IDsVisitor::visitBindingsExpr(psr::BindingsExprContext* ctx) { return visit(ctx->expr()); }

std::any IDsVisitor::visitBindingsFormula(psr::BindingsFormulaContext* ctx) { return visit(ctx->formula()); }

std::any IDsVisitor::visitPartialAppl(psr::PartialApplContext* ctx) {
  StringSet deps;

  auto base_deps = std::any_cast<StringSet>(visit(ctx->applBase()));

  std::set_union(deps.begin(), deps.end(), base_deps.begin(), base_deps.end(), std::inserter(deps, deps.begin()));

  auto params_deps = std::any_cast<StringSet>(visit(ctx->applParams()));

  std::set_union(deps.begin(), deps.end(), params_deps.begin(), params_deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitFullAppl(psr::FullApplContext* ctx) {
  StringSet deps;

  auto base_deps = std::any_cast<StringSet>(visit(ctx->applBase()));

  std::set_union(deps.begin(), deps.end(), base_deps.begin(), base_deps.end(), std::inserter(deps, deps.begin()));

  auto params_deps = std::any_cast<StringSet>(visit(ctx->applParams()));

  std::set_union(deps.begin(), deps.end(), params_deps.begin(), params_deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitBinOp(psr::BinOpContext* ctx) {
  StringSet deps;

  auto lhs_deps = std::any_cast<StringSet>(visit(ctx->lhs));
  std::set_union(deps.begin(), deps.end(), deps.begin(), deps.end(), std::inserter(deps, deps.begin()));

  auto rhs_deps = std::any_cast<StringSet>(visit(ctx->rhs));
  std::set_union(deps.begin(), deps.end(), deps.begin(), deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitUnOp(psr::UnOpContext* ctx) { return visit(ctx->formula()); }

std::any IDsVisitor::visitQuantification(psr::QuantificationContext* ctx) { return visit(ctx->formula()); }

std::any IDsVisitor::visitParen(psr::ParenContext* ctx) { return visit(ctx->formula()); }

std::any IDsVisitor::visitComparison(psr::ComparisonContext* ctx) {
  StringSet deps;

  auto lhs_deps = std::any_cast<StringSet>(visit(ctx->lhs));
  std::set_union(deps.begin(), deps.end(), deps.begin(), deps.end(), std::inserter(deps, deps.begin()));

  auto rhs_deps = std::any_cast<StringSet>(visit(ctx->rhs));
  std::set_union(deps.begin(), deps.end(), deps.begin(), deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitBindingInner(psr::BindingInnerContext* ctx) {
  StringSet deps;

  for (auto& child_ctx : ctx->binding()) {
    auto child_deps = std::any_cast<StringSet>(visit(child_ctx));
    std::set_union(deps.begin(), deps.end(), child_deps.begin(), child_deps.end(), std::inserter(deps, deps.begin()));
  }

  return deps;
}

std::any IDsVisitor::visitBinding(psr::BindingContext* ctx) {
  StringSet deps;

  if (ctx->id_domain) {
    std::string id = ctx->id->getText();
    ast_->AddVar(id);
  }

  return deps;
}

std::any IDsVisitor::visitApplBase(psr::ApplBaseContext* ctx) {
  StringSet deps;

  if (ctx->relAbs()) {
    return visit(ctx->relAbs());
  }

  if (ctx->T_ID()) {
    std::string id = ctx->T_ID()->getText();
    deps.insert(id);
  }

  return deps;
}

std::any IDsVisitor::visitApplParams(psr::ApplParamsContext* ctx) {
  StringSet deps;

  for (auto& child_ctx : ctx->applParam()) {
    auto child_deps = std::any_cast<StringSet>(visit(child_ctx));
    std::set_union(deps.begin(), deps.end(), child_deps.begin(), child_deps.end(), std::inserter(deps, deps.begin()));
  }

  return deps;
}

std::any IDsVisitor::visitApplParam(psr::ApplParamContext* ctx) {
  StringSet deps;

  if (ctx->expr()) {
    auto child_deps = std::any_cast<StringSet>(visit(ctx->expr()));
    std::set_union(deps.begin(), deps.end(), child_deps.begin(), child_deps.end(), std::inserter(deps, deps.begin()));
  }

  return deps;
}

}  // namespace rel2sql
