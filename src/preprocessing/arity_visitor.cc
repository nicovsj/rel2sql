#include "preprocessing/arity_visitor.h"

#include "sql/aggregate_map.h"
#include "support/exceptions.h"

namespace rel2sql {

namespace {

SourceLocation GetSourceLocationFromNode(RelNode* node) {
  if (!node || !node->ctx) return SourceLocation(0, 0);
  auto* ctx = node->ctx;
  int line = ctx->getStart() ? ctx->getStart()->getLine() : 0;
  int column = ctx->getStart() ? ctx->getStart()->getCharPositionInLine() : 0;
  std::string text_snippet = ctx->getText();
  if (text_snippet.length() > 100) text_snippet = text_snippet.substr(0, 97) + "...";
  return SourceLocation(line, column, text_snippet);
}

}  // namespace

std::shared_ptr<RelProgram> ArityVisitor::Visit(const std::shared_ptr<RelProgram>& node) {
  defs_by_id_.clear();
  for (auto& def : node->defs) {
    if (!def) continue;
    std::string id = def->name;
    auto it = defs_by_id_.find(id);
    if (it != defs_by_id_.end()) {
      // Duplicate def: add body to first def's multiple_defs, disable this one
      if (!it->second.empty() && it->second[0]->body) {
        it->second[0]->multiple_defs.push_back(def->body);
      }
      def->disabled = true;
    }
    defs_by_id_[id].push_back(def);
  }

  for (const auto& id : container_->SortedIDs()) {
    if (GetAggregateMap().find(id) != GetAggregateMap().end()) continue;
    if (container_->IsEDB(id)) continue;
    auto it = defs_by_id_.find(id);
    if (it == defs_by_id_.end()) {
      SourceLocation loc(0, 0);
      if (!defs_by_id_.empty() && !defs_by_id_.begin()->second.empty()) {
        loc = GetSourceLocationFromNode(defs_by_id_.begin()->second[0].get());
      }
      throw ArityException("IDB '" + id + "' is not defined", loc);
    }
    for (auto& def : it->second) {
      if (def) Visit(def);
    }
  }
  return node;
}

std::shared_ptr<RelDef> ArityVisitor::Visit(const std::shared_ptr<RelDef>& node) {
  if (node->body) Visit(node->body);
  node->arity = node->body ? node->body->arity : 0;
  container_->AddIDB(node->name, static_cast<int>(node->arity));
  return node;
}

std::shared_ptr<RelAbstraction> ArityVisitor::Visit(const std::shared_ptr<RelAbstraction>& node) {
  if (node->exprs.empty()) return node;
  Visit(node->exprs[0]);
  size_t common_arity = node->exprs[0]->arity;
  for (size_t i = 1; i < node->exprs.size(); ++i) {
    Visit(node->exprs[i]);
    if (node->exprs[i]->arity != common_arity) {
      throw ArityException("Arity mismatch in relational abstraction: expected " + std::to_string(common_arity) +
                               ", got " + std::to_string(node->exprs[i]->arity),
                           GetSourceLocationFromNode(node->exprs[i].get()));
    }
  }
  node->arity = common_arity;
  return node;
}

std::shared_ptr<RelExpr> ArityVisitor::Visit(const std::shared_ptr<RelLiteral>& node) {
  node->arity = 1;
  return node;
}

std::shared_ptr<RelExpr> ArityVisitor::Visit(const std::shared_ptr<RelProduct>& node) {
  node->arity = 0;
  for (auto& expr : node->exprs) {
    if (expr) {
      Visit(expr);
      node->arity += expr->arity;
    }
  }
  return node;
}

std::shared_ptr<RelExpr> ArityVisitor::Visit(const std::shared_ptr<RelConditionExpr>& node) {
  if (node->lhs) Visit(node->lhs);
  if (node->rhs) Visit(node->rhs);
  node->arity = node->lhs ? node->lhs->arity : 0;
  return node;
}

std::shared_ptr<RelExpr> ArityVisitor::Visit(const std::shared_ptr<RelAbstractionExpr>& node) {
  if (node->rel_abs) Visit(node->rel_abs);
  node->arity = node->rel_abs ? node->rel_abs->arity : 0;
  return node;
}

std::shared_ptr<RelExpr> ArityVisitor::Visit(const std::shared_ptr<RelFormulaExpr>& node) {
  if (node->formula) Visit(node->formula);
  node->arity = 0;
  return node;
}

std::shared_ptr<RelExpr> ArityVisitor::Visit(const std::shared_ptr<RelBindingsExpr>& node) {
  if (node->expr) Visit(node->expr);
  node->arity = (node->expr ? node->expr->arity : 0) + static_cast<size_t>(node->bindings.size());
  return node;
}

std::shared_ptr<RelExpr> ArityVisitor::Visit(const std::shared_ptr<RelBindingsFormula>& node) {
  if (node->formula) Visit(node->formula);
  node->arity = node->bindings.size();
  return node;
}

std::shared_ptr<RelExpr> ArityVisitor::Visit(const std::shared_ptr<RelPartialAppl>& node) {
  int base_arity = GetArityFromBase(node->base);
  int params_arity = GetArityFromParams(node->params);
  int result = base_arity - params_arity;

  // If the base is an aggregate function, the arity is 1
  if (auto* id_base = dynamic_cast<RelIDApplBase*>(node->base.get())) {
    if (GetAggregateMap().find(id_base->id) != GetAggregateMap().end()) {
      result = 1;
    }
  }

  if (result < 0) {
    throw std::runtime_error("Partial application overflows the arity of the base expression");
  }
  node->arity = static_cast<size_t>(result);
  return node;
}

std::shared_ptr<RelFormula> ArityVisitor::Visit(const std::shared_ptr<RelFullAppl>& node) {
  GetArityFromBase(node->base);
  GetArityFromParams(node->params);
  node->arity = 0;
  return node;
}

std::shared_ptr<RelTerm> ArityVisitor::Visit(const std::shared_ptr<RelIDTerm>& node) {
  auto info = container_->GetRelationInfo(node->id);
  if (info) {
    node->arity = static_cast<size_t>(info->arity);
  } else {
    node->arity = 1;  // Variable
  }
  return node;
}

std::shared_ptr<RelTerm> ArityVisitor::Visit(const std::shared_ptr<RelNumTerm>& node) {
  node->arity = 1;
  return node;
}

std::shared_ptr<RelTerm> ArityVisitor::Visit(const std::shared_ptr<RelOpTerm>& node) {
  node->arity = 1;
  return node;
}

std::shared_ptr<RelTerm> ArityVisitor::Visit(const std::shared_ptr<RelParenthesisTerm>& node) {
  if (node->term) Visit(node->term);
  node->arity = node->term ? node->term->arity : 0;
  return node;
}

int ArityVisitor::GetArityFromBase(const std::shared_ptr<RelApplBase>& base) {
  if (auto* id_base = dynamic_cast<RelIDApplBase*>(base.get())) {
    if (GetAggregateMap().find(id_base->id) != GetAggregateMap().end()) return 1;
    auto info = container_->GetRelationInfo(id_base->id);
    if (info) return info->arity;
    throw ArityException("Relation '" + id_base->id + "' is not defined", SourceLocation(0, 0));
  }
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(base.get())) {
    if (abs_base->rel_abs) Visit(abs_base->rel_abs);
    return abs_base->rel_abs ? static_cast<int>(abs_base->rel_abs->arity) : 0;
  }
  return 0;
}

int ArityVisitor::GetArityFromParams(const std::vector<std::shared_ptr<RelApplParam>>& params) {
  int total = 0;
  for (const auto& param : params) {
    if (param && param->GetExpr()) {
      Visit(param->GetExpr());
      total += static_cast<int>(param->GetExpr()->arity);
    }
  }
  return total;
}

}  // namespace rel2sql
