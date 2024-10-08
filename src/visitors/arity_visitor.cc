#include "arity_visitor.h"

ArityVisitor::ArityVisitor(std::shared_ptr<ExtendedASTData> data) : BaseVisitor(data) {}

std::any ArityVisitor::visitProgram(psr::ProgramContext *ctx) {
  std::unordered_map<std::string, std::vector<psr::RelDefContext *>> defs_by_id;

  for (auto &child_ctx : ctx->relDef()) {
    std::string id = child_ctx->name->getText();
    auto found_def = defs_by_id.find(id);
    std::string text = child_ctx->getText();
    auto &current_node = GetNode(child_ctx);
    // If the def is already in the map, it means that the relation has multiple defs
    if (found_def != defs_by_id.end()) {
      auto &found_node = GetNode(found_def->second[0]->relAbs());
      found_node.multiple_defs.push_back(child_ctx);
      current_node.disabled = true;
    }
    defs_by_id[child_ctx->name->getText()].push_back(child_ctx);
  }

  for (auto &id : ast_data_->sorted_ids) {
    if (AGGREGATE_MAP.find(id) != AGGREGATE_MAP.end()) {
      // Skip aggregate functions
      continue;
    }
    if (defs_by_id.find(id) == defs_by_id.end()) {
      throw std::runtime_error("IDB " + id + " is not defined");
    }

    for (auto &def : defs_by_id[id]) {
      visit(def);
    }
  }

  return {};
}

std::any ArityVisitor::visitRelDef(psr::RelDefContext *ctx) {
  visit(ctx->relAbs());

  GetNode(ctx).arity = GetNode(ctx->relAbs()).arity;

  ast_data_->arity_by_id[ctx->name->getText()] = GetNode(ctx).arity;

  return {};
}

std::any ArityVisitor::visitRelAbs(psr::RelAbsContext *ctx) {
  visit(ctx->expr(0));

  int common_arity = GetNode(ctx->expr(0)).arity;

  for (int i = 1; i < ctx->expr().size(); i++) {
    visit(ctx->expr(i));

    if (GetNode(ctx->expr(i)).arity != common_arity) {
      throw std::runtime_error("Not every member with the same arity in relational abstraction");
    }
  }

  GetNode(ctx).arity = common_arity;

  return {};
}

std::any ArityVisitor::visitLitExpr(psr::LitExprContext *ctx) {
  GetNode(ctx).arity = 1;
  return {};
}

std::any ArityVisitor::visitIDExpr(psr::IDExprContext *ctx) {
  std::string id = ctx->T_ID()->getText();

  if (auto found = ast_data_->arity_by_id.find(id); found != ast_data_->arity_by_id.end()) {
    GetNode(ctx).arity = found->second;
  } else {
    GetNode(ctx).arity = 1;
  }

  return {};
}

std::any ArityVisitor::visitProductExpr(psr::ProductExprContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  for (auto &child : ctx->productInner()->expr()) {
    visit(child);
    node.arity += GetNode(child).arity;
  }

  return {};
}

std::any ArityVisitor::visitConditionExpr(psr::ConditionExprContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  visit(ctx->lhs);

  visit(ctx->rhs);

  node.arity = GetNode(ctx->lhs).arity;

  return {};
}

std::any ArityVisitor::visitRelAbsExpr(psr::RelAbsExprContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  visit(ctx->relAbs());

  node.arity = GetNode(ctx->relAbs()).arity;

  return {};
}

std::any ArityVisitor::visitFormulaExpr(psr::FormulaExprContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  visit(ctx->formula());

  node.arity = 0;

  return {};
}

std::any ArityVisitor::visitBindingsExpr(psr::BindingsExprContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  visit(ctx->expr());

  node.arity = GetNode(ctx->expr()).arity + ctx->bindingInner()->binding().size();

  return {};
}

std::any ArityVisitor::visitBindingsFormula(psr::BindingsFormulaContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  node.arity = ctx->bindingInner()->binding().size();

  return {};
}

std::any ArityVisitor::visitPartialAppl(psr::PartialApplContext *ctx) {
  visit(ctx->applBase());
  auto base_arity = GetNode(ctx->applBase()).arity;

  for (auto &child : ctx->applParams()->applParam()) {
    visit(child->expr());
    auto param_arity = GetNode(child->expr()).arity;
    base_arity -= param_arity;
  }

  if (ctx->applBase()->T_ID()) {
    // If the base is an aggregate function, the arity is 1
    std::string id = ctx->applBase()->T_ID()->getText();
    if (auto found = AGGREGATE_MAP.find(id); found != AGGREGATE_MAP.end()) {
      base_arity = 1;
    }
  }

  GetNode(ctx).arity = base_arity;

  if (base_arity < 0) {
    throw std::runtime_error("Partial application overflows the arity of the base expression");
  }

  return {};
}

std::any ArityVisitor::visitApplBase(psr::ApplBaseContext *ctx) {
  if (ctx->T_ID()) {
    std::string id = ctx->T_ID()->getText();
    if (auto found = ast_data_->arity_by_id.find(id); found != ast_data_->arity_by_id.end()) {
      GetNode(ctx).arity = found->second;
    } else {
      GetNode(ctx).arity = 1;
    }
  } else {
    visit(ctx->relAbs());
    GetNode(ctx).arity = GetNode(ctx->relAbs()).arity;
  }

  return {};
}

std::any ArityVisitor::visitApplParams(psr::ApplParamsContext *ctx) {
  ExtendedNode &node = GetNode(ctx);

  node.arity = 0;

  for (auto &child : ctx->applParam()) {
    visit(child->expr());
    node.arity += GetNode(child).arity;
  }

  return {};
}
