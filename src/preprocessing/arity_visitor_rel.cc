#include "preprocessing/arity_visitor_rel.h"

#include "rel_ast/extended_ast.h"
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

void ArityVisitorRel::Visit(RelProgram& node) {
  defs_by_id_.clear();
  for (auto& def : node.defs) {
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
    if (AGGREGATE_MAP.find(id) != AGGREGATE_MAP.end()) continue;
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
      if (def) def->Accept(*this);
    }
  }
}

void ArityVisitorRel::Visit(RelDef& node) {
  if (node.body) node.body->Accept(*this);
  node.arity = node.body ? node.body->arity : 0;
  container_->AddIDB(node.name, static_cast<int>(node.arity));
}

void ArityVisitorRel::Visit(RelAbstraction& node) {
  if (node.exprs.empty()) return;
  node.exprs[0]->Accept(*this);
  size_t common_arity = node.exprs[0]->arity;
  for (size_t i = 1; i < node.exprs.size(); ++i) {
    node.exprs[i]->Accept(*this);
    if (node.exprs[i]->arity != common_arity) {
      throw ArityException("Arity mismatch in relational abstraction: expected " + std::to_string(common_arity) +
                               ", got " + std::to_string(node.exprs[i]->arity),
                           GetSourceLocationFromNode(node.exprs[i].get()));
    }
  }
  node.arity = common_arity;
}

void ArityVisitorRel::Visit(RelLitExpr& node) { node.arity = 1; }

void ArityVisitorRel::Visit(RelTermExpr& node) {
  if (node.term) node.term->Accept(*this);
  node.arity = node.term ? node.term->arity : 0;
}

void ArityVisitorRel::Visit(RelProductExpr& node) {
  node.arity = 0;
  for (auto& expr : node.exprs) {
    if (expr) {
      expr->Accept(*this);
      node.arity += expr->arity;
    }
  }
}

void ArityVisitorRel::Visit(RelConditionExpr& node) {
  if (node.lhs) node.lhs->Accept(*this);
  if (node.rhs) node.rhs->Accept(*this);
  node.arity = node.lhs ? node.lhs->arity : 0;
}

void ArityVisitorRel::Visit(RelAbstractionExpr& node) {
  if (node.rel_abs) node.rel_abs->Accept(*this);
  node.arity = node.rel_abs ? node.rel_abs->arity : 0;
}

void ArityVisitorRel::Visit(RelFormulaExpr& node) {
  if (node.formula) node.formula->Accept(*this);
  node.arity = 0;
}

void ArityVisitorRel::Visit(RelBindingsExpr& node) {
  if (node.expr) node.expr->Accept(*this);
  node.arity = (node.expr ? node.expr->arity : 0) + static_cast<size_t>(node.bindings.size());
}

void ArityVisitorRel::Visit(RelBindingsFormula& node) {
  if (node.formula) node.formula->Accept(*this);
  node.arity = node.bindings.size();
}

void ArityVisitorRel::Visit(RelPartialAppl& node) {
  int base_arity = GetArityFromBase(node.base);
  int params_arity = GetArityFromParams(node.params);
  int result = base_arity - params_arity;

  // If the base is an aggregate function, the arity is 1
  if (auto* id_base = dynamic_cast<RelIDApplBase*>(node.base.get())) {
    if (AGGREGATE_MAP.find(id_base->id) != AGGREGATE_MAP.end()) {
      result = 1;
    }
  }

  if (result < 0) {
    throw std::runtime_error("Partial application overflows the arity of the base expression");
  }
  node.arity = static_cast<size_t>(result);
}

void ArityVisitorRel::Visit(RelFullAppl& node) {
  GetArityFromBase(node.base);
  GetArityFromParams(node.params);
  node.arity = 0;
}

void ArityVisitorRel::Visit(RelIDTerm& node) {
  auto info = container_->GetRelationInfo(node.id);
  if (info) {
    node.arity = static_cast<size_t>(info->arity);
  } else {
    node.arity = 1;  // Variable
  }
}

void ArityVisitorRel::Visit(RelNumTerm& node) { node.arity = 1; }

void ArityVisitorRel::Visit(RelOpTerm& node) { node.arity = 1; }

void ArityVisitorRel::Visit(RelParenthesisTerm& node) {
  if (node.term) node.term->Accept(*this);
  node.arity = node.term ? node.term->arity : 0;
}

int ArityVisitorRel::GetArityFromBase(const std::shared_ptr<RelApplBase>& base) {
  if (auto* id_base = dynamic_cast<RelIDApplBase*>(base.get())) {
    if (AGGREGATE_MAP.find(id_base->id) != AGGREGATE_MAP.end()) return 1;
    auto info = container_->GetRelationInfo(id_base->id);
    if (info) return info->arity;
    throw ArityException("Relation '" + id_base->id + "' is not defined", SourceLocation(0, 0));
  }
  if (auto* abs_base = dynamic_cast<RelAbstractionApplBase*>(base.get())) {
    if (abs_base->rel_abs) abs_base->rel_abs->Accept(*this);
    return abs_base->rel_abs ? static_cast<int>(abs_base->rel_abs->arity) : 0;
  }
  return 0;
}

int ArityVisitorRel::GetArityFromParams(const std::vector<std::shared_ptr<RelApplParam>>& params) {
  int total = 0;
  for (const auto& param : params) {
    if (param && param->GetExpr()) {
      param->GetExpr()->Accept(*this);
      total += static_cast<int>(param->GetExpr()->arity);
    }
  }
  return total;
}

}  // namespace rel2sql
