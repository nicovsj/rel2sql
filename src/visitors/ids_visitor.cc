#include "ids_visitor.h"

#include <queue>

using str_set = std::unordered_set<std::string>;

IDsVisitor::IDsVisitor(std::shared_ptr<ExtendedASTData> data) : BaseVisitor(data) {}

std::any IDsVisitor::visitProgram(psr::ProgramContext *ctx) {
  for (auto &child_ctx : ctx->relDef()) {
    visit(child_ctx);
  }

  RemoveVarsFromDependencyGraph();

  ast_data_->sorted_ids = InverseTopologicalOrderOfDependencyGraph();
  ;

  return {};
}

std::any IDsVisitor::visitRelDef(psr::RelDefContext *ctx) {
  std::string id = ctx->T_ID()->getText();

  ast_data_->AddIDB(id);  // If defined in the program, it is an IDB

  auto deps = std::any_cast<str_set>(visit(ctx->relAbs()));

  for (const auto &dep : deps) {
    ast_data_->AddDependency(id, dep);
  }

  return {};
}

std::any IDsVisitor::visitRelAbs(psr::RelAbsContext *ctx) {
  str_set deps;

  for (auto &child_ctx : ctx->expr()) {
    auto child_deps = std::any_cast<str_set>(visit(child_ctx));
    std::set_union(deps.begin(), deps.end(), child_deps.begin(), child_deps.end(), std::inserter(deps, deps.begin()));
  }

  return deps;
}

std::any IDsVisitor::visitIDTerm(psr::IDTermContext *ctx) {
  std::string id = ctx->T_ID()->getText();

  ast_data_->AddVar(id);

  return str_set{id};
}

std::any IDsVisitor::visitNumTerm(psr::NumTermContext *ctx) { return str_set{}; }

std::any IDsVisitor::visitOpTerm(psr::OpTermContext *ctx) {
  str_set deps;

  auto lhs_deps = std::any_cast<str_set>(visit(ctx->lhs));
  std::set_union(deps.begin(), deps.end(), lhs_deps.begin(), lhs_deps.end(), std::inserter(deps, deps.begin()));

  auto rhs_deps = std::any_cast<str_set>(visit(ctx->rhs));
  std::set_union(deps.begin(), deps.end(), rhs_deps.begin(), rhs_deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitLitExpr(psr::LitExprContext *ctx) { return str_set{}; }

std::any IDsVisitor::visitIDExpr(psr::IDExprContext *ctx) {
  std::string id = ctx->T_ID()->getText();

  ast_data_->AddVar(id);

  return str_set{id};
}

std::any IDsVisitor::visitProductExpr(psr::ProductExprContext *ctx) {
  str_set deps;

  for (auto &child_ctx : ctx->productInner()->expr()) {
    auto child_deps = std::any_cast<str_set>(visit(child_ctx));
    std::set_union(deps.begin(), deps.end(), child_deps.begin(), child_deps.end(), std::inserter(deps, deps.begin()));
  }

  return deps;
}

std::any IDsVisitor::visitConditionExpr(psr::ConditionExprContext *ctx) {
  str_set deps;

  auto lhs_deps = std::any_cast<str_set>(visit(ctx->lhs));
  std::set_union(deps.begin(), deps.end(), deps.begin(), deps.end(), std::inserter(deps, deps.begin()));

  auto rhs_deps = std::any_cast<str_set>(visit(ctx->rhs));
  std::set_union(deps.begin(), deps.end(), deps.begin(), deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitRelAbsExpr(psr::RelAbsExprContext *ctx) {
  return visit(ctx->relAbs());
  ;
}

std::any IDsVisitor::visitFormulaExpr(psr::FormulaExprContext *ctx) { return visit(ctx->formula()); }

std::any IDsVisitor::visitBindingsExpr(psr::BindingsExprContext *ctx) { return visit(ctx->expr()); }

std::any IDsVisitor::visitBindingsFormula(psr::BindingsFormulaContext *ctx) { return visit(ctx->formula()); }

std::any IDsVisitor::visitPartialAppl(psr::PartialApplContext *ctx) {
  str_set deps;

  auto base_deps = std::any_cast<str_set>(visit(ctx->applBase()));

  std::set_union(deps.begin(), deps.end(), base_deps.begin(), base_deps.end(), std::inserter(deps, deps.begin()));

  auto params_deps = std::any_cast<str_set>(visit(ctx->applParams()));

  std::set_union(deps.begin(), deps.end(), params_deps.begin(), params_deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitFullAppl(psr::FullApplContext *ctx) {
  str_set deps;

  auto base_deps = std::any_cast<str_set>(visit(ctx->applBase()));

  std::set_union(deps.begin(), deps.end(), base_deps.begin(), base_deps.end(), std::inserter(deps, deps.begin()));

  auto params_deps = std::any_cast<str_set>(visit(ctx->applParams()));

  std::set_union(deps.begin(), deps.end(), params_deps.begin(), params_deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitBinOp(psr::BinOpContext *ctx) {
  str_set deps;

  auto lhs_deps = std::any_cast<str_set>(visit(ctx->lhs));
  std::set_union(deps.begin(), deps.end(), deps.begin(), deps.end(), std::inserter(deps, deps.begin()));

  auto rhs_deps = std::any_cast<str_set>(visit(ctx->rhs));
  std::set_union(deps.begin(), deps.end(), deps.begin(), deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitUnOp(psr::UnOpContext *ctx) { return visit(ctx->formula()); }

std::any IDsVisitor::visitQuantification(psr::QuantificationContext *ctx) { return visit(ctx->formula()); }

std::any IDsVisitor::visitParen(psr::ParenContext *ctx) { return visit(ctx->formula()); }

std::any IDsVisitor::visitComparison(psr::ComparisonContext *ctx) {
  str_set deps;

  auto lhs_deps = std::any_cast<str_set>(visit(ctx->lhs));
  std::set_union(deps.begin(), deps.end(), deps.begin(), deps.end(), std::inserter(deps, deps.begin()));

  auto rhs_deps = std::any_cast<str_set>(visit(ctx->rhs));
  std::set_union(deps.begin(), deps.end(), deps.begin(), deps.end(), std::inserter(deps, deps.begin()));

  return deps;
}

std::any IDsVisitor::visitBindingInner(psr::BindingInnerContext *ctx) {
  str_set deps;

  for (auto &child_ctx : ctx->binding()) {
    auto child_deps = std::any_cast<str_set>(visit(child_ctx));
    std::set_union(deps.begin(), deps.end(), child_deps.begin(), child_deps.end(), std::inserter(deps, deps.begin()));
  }

  return deps;
}

std::any IDsVisitor::visitBinding(psr::BindingContext *ctx) {
  str_set deps;

  if (ctx->id_domain) {
    std::string id = ctx->id->getText();
    ast_data_->AddVar(id);
  }

  return deps;
}

std::any IDsVisitor::visitApplBase(psr::ApplBaseContext *ctx) {
  str_set deps;

  if (ctx->relAbs()) {
    return visit(ctx->relAbs());
  }

  if (ctx->T_ID()) {
    std::string id = ctx->T_ID()->getText();
    deps.insert(id);
  }

  return deps;
}

std::any IDsVisitor::visitApplParams(psr::ApplParamsContext *ctx) {
  str_set deps;

  for (auto &child_ctx : ctx->applParam()) {
    auto child_deps = std::any_cast<str_set>(visit(child_ctx));
    std::set_union(deps.begin(), deps.end(), child_deps.begin(), child_deps.end(), std::inserter(deps, deps.begin()));
  }

  return deps;
}

std::any IDsVisitor::visitApplParam(psr::ApplParamContext *ctx) {
  str_set deps;

  if (ctx->expr()) {
    auto child_deps = std::any_cast<str_set>(visit(ctx->expr()));
    std::set_union(deps.begin(), deps.end(), child_deps.begin(), child_deps.end(), std::inserter(deps, deps.begin()));
  }

  return deps;
}

void IDsVisitor::RemoveVarsFromDependencyGraph() {
  for (const auto &id : ast_data_->ids) {
    if (ast_data_->vars.find(id) != ast_data_->vars.end()) {
      ast_data_->ids_dependencies.erase(id);
      continue;
    }
    std::vector<std::string> real_deps;
    for (const auto &dep : ast_data_->ids_dependencies[id]) {
      if (ast_data_->vars.find(dep) == ast_data_->vars.end()) {
        real_deps.push_back(dep);
      }
    }
    ast_data_->ids_dependencies[id] = std::move(real_deps);
  }
}

std::vector<std::string> IDsVisitor::InverseTopologicalOrderOfDependencyGraph() {
  std::unordered_map<std::string, int> in_degree;
  std::unordered_map<std::string, std::vector<std::string>> graph = ast_data_->ids_dependencies;

  for (const auto &[id, deps] : graph) {
    in_degree[id] = 0;
  }

  for (const auto &[id, deps] : graph) {
    for (const auto &dep : deps) {
      in_degree[dep]++;
    }
  }

  std::queue<std::string> q;
  for (const auto &[id, degree] : in_degree) {
    if (degree == 0) {
      q.push(id);
    }
  }

  std::vector<std::string> order;
  while (!q.empty()) {
    std::string id = q.front();
    q.pop();
    order.push_back(id);

    for (const auto &dep : graph[id]) {
      in_degree[dep]--;
      if (in_degree[dep] == 0) {
        q.push(dep);
      }
    }
  }

  std::reverse(order.begin(), order.end());

  return order;
}
