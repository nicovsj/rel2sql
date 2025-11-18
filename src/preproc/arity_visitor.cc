#include "arity_visitor.h"

#include "utils/exceptions.h"

namespace rel2sql {

ArityVisitor::ArityVisitor(std::shared_ptr<RelAST> ast) : BaseVisitor(ast) {}

std::any ArityVisitor::visitProgram(psr::ProgramContext* ctx) {
  std::unordered_map<std::string, std::vector<psr::RelDefContext*>> defs_by_id;

  for (auto& child_ctx : ctx->relDef()) {
    std::string id = child_ctx->name->getText();
    auto found_def = defs_by_id.find(id);
    std::string text = child_ctx->getText();
    auto current_node = GetNode(child_ctx);
    // If the def is already in the map, it means that the relation has multiple defs
    if (found_def != defs_by_id.end()) {
      auto found_node = GetNode(found_def->second[0]->relAbs());
      found_node->multiple_defs.push_back(child_ctx);
      current_node->disabled = true;
    }
    defs_by_id[child_ctx->name->getText()].push_back(child_ctx);
  }

  for (auto& id : ast_->SortedIDs()) {
    if (AGGREGATE_MAP.find(id) != AGGREGATE_MAP.end()) {
      // Skip aggregate functions
      continue;
    }
    if (ast_->IsEDB(id)) {
      // Skip external databases
      continue;
    }
    if (defs_by_id.find(id) == defs_by_id.end()) {
      // Find a context to get location info - use the first definition as reference
      antlr4::ParserRuleContext* ctx = nullptr;
      for (auto& pair : defs_by_id) {
        if (!pair.second.empty()) {
          ctx = pair.second[0];
          break;
        }
      }

      SourceLocation location = ctx ? GetSourceLocation(ctx) : SourceLocation(0, 0);
      throw ArityException("IDB '" + id + "' is not defined", location);
    }

    for (auto& def : defs_by_id[id]) {
      visit(def);
    }
  }

  return {};
}

std::any ArityVisitor::visitRelDef(psr::RelDefContext* ctx) {
  visit(ctx->relAbs());

  GetNode(ctx)->arity = GetNode(ctx->relAbs())->arity;

  ast_->AddIDB(ctx->name->getText(), GetNode(ctx)->arity);

  return {};
}

std::any ArityVisitor::visitRelAbs(psr::RelAbsContext* ctx) {
  visit(ctx->expr(0));

  int common_arity = GetNode(ctx->expr(0))->arity;

  for (int i = 1; i < ctx->expr().size(); i++) {
    visit(ctx->expr(i));

    if (GetNode(ctx->expr(i))->arity != common_arity) {
      SourceLocation location = GetSourceLocation(ctx->expr(i));
      throw ArityException("Arity mismatch in relational abstraction: expected " + std::to_string(common_arity) +
                               ", got " + std::to_string(GetNode(ctx->expr(i))->arity),
                           location);
    }
  }

  GetNode(ctx)->arity = common_arity;

  return {};
}

std::any ArityVisitor::visitLitExpr(psr::LitExprContext* ctx) {
  GetNode(ctx)->arity = 1;
  return {};
}

std::any ArityVisitor::visitIDExpr(psr::IDExprContext* ctx) {
  std::string id = ctx->T_ID()->getText();

  if (auto found = ast_->GetRelationInfo(id); found != std::nullopt) {
    GetNode(ctx)->arity = found->arity;
  } else {
    // If a relation is not found, it might be a variable
    GetNode(ctx)->arity = 1;
  }

  return {};
}

std::any ArityVisitor::visitProductExpr(psr::ProductExprContext* ctx) {
  auto node = GetNode(ctx);

  for (auto& child : ctx->productInner()->expr()) {
    visit(child);
    node->arity += GetNode(child)->arity;
  }

  return {};
}

std::any ArityVisitor::visitConditionExpr(psr::ConditionExprContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->lhs);

  visit(ctx->rhs);

  node->arity = GetNode(ctx->lhs)->arity;

  return {};
}

std::any ArityVisitor::visitRelAbsExpr(psr::RelAbsExprContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->relAbs());

  node->arity = GetNode(ctx->relAbs())->arity;

  return {};
}

std::any ArityVisitor::visitFormulaExpr(psr::FormulaExprContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->formula());

  node->arity = 0;

  return {};
}

std::any ArityVisitor::visitBindingsExpr(psr::BindingsExprContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->expr());

  node->arity = GetNode(ctx->expr())->arity + ctx->bindingInner()->binding().size();

  return {};
}

std::any ArityVisitor::visitBindingsFormula(psr::BindingsFormulaContext* ctx) {
  auto node = GetNode(ctx);

  visit(ctx->formula());

  node->arity = ctx->bindingInner()->binding().size();

  return {};
}

std::any ArityVisitor::visitPartialAppl(psr::PartialApplContext* ctx) {
  visit(ctx->applBase());
  auto base_arity = GetNode(ctx->applBase())->arity;

  for (auto& child : ctx->applParams()->applParam()) {
    visit(child->expr());
    auto param_arity = GetNode(child->expr())->arity;
    base_arity -= param_arity;
  }

  if (ctx->applBase()->T_ID()) {
    // If the base is an aggregate function, the arity is 1
    std::string id = ctx->applBase()->T_ID()->getText();
    if (auto found = AGGREGATE_MAP.find(id); found != AGGREGATE_MAP.end()) {
      base_arity = 1;
    }
  }

  GetNode(ctx)->arity = base_arity;

  if (base_arity < 0) {
    throw std::runtime_error("Partial application overflows the arity of the base expression");
  }

  return {};
}

std::any ArityVisitor::visitFullAppl(psr::FullApplContext* ctx) {
  visit(ctx->applBase());
  visit(ctx->applParams());

  // Arity is zero so no need to set it
  return {};
}

std::any ArityVisitor::visitBinOp(psr::BinOpContext* ctx) {
  visit(ctx->lhs);
  visit(ctx->rhs);

  // Arity is zero so no need to set it
  return {};
}

std::any ArityVisitor::visitUnOp(psr::UnOpContext* ctx) {
  visit(ctx->formula());

  // Arity is zero so no need to set it
  return {};
}

std::any ArityVisitor::visitQuantification(psr::QuantificationContext* ctx) {
  visit(ctx->formula());

  // Arity is zero so no need to set it
  return {};
}

std::any ArityVisitor::visitParen(psr::ParenContext* ctx) {
  visit(ctx->formula());

  // Arity is zero so no need to set it
  return {};
}

std::any ArityVisitor::visitComparison(psr::ComparisonContext* ctx) {
  visit(ctx->lhs);
  visit(ctx->rhs);

  // Arity is zero so no need to set it
  return {};
}

std::any ArityVisitor::visitApplBase(psr::ApplBaseContext* ctx) {
  if (ctx->T_ID()) {
    std::string id = ctx->T_ID()->getText();
    auto found = ast_->GetRelationInfo(id);
    if (found != std::nullopt) {
      GetNode(ctx)->arity = found->arity;
    } else if (AGGREGATE_MAP.find(id) != AGGREGATE_MAP.end()) {
      // Aggregate functions always produce a single column
      GetNode(ctx)->arity = 1;
    } else {
      throw ArityException("Relation '" + id + "' is not defined", GetSourceLocation(ctx));
    }
  } else {
    visit(ctx->relAbs());
    GetNode(ctx)->arity = GetNode(ctx->relAbs())->arity;
  }

  return {};
}

std::any ArityVisitor::visitApplParams(psr::ApplParamsContext* ctx) {
  auto node = GetNode(ctx);

  node->arity = 0;

  for (auto& child : ctx->applParam()) {
    visit(child->expr());
    node->arity += GetNode(child)->arity;
  }

  return {};
}

}  // namespace rel2sql
