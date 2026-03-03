#include "preprocessing/fixpoint_safety_visitor.h"

#include <cctype>

#include "rel_ast/projection.h"
#include "support/exceptions.h"

namespace rel2sql {

FixpointSafetyVisitor::FixpointSafetyVisitor(RelContext* container, const std::string& recursive_relation)
    : SafetyVisitor(container), recursive_relation_(recursive_relation), iteration_count_(0) {
  current_placeholder_ = "R0";
}

std::vector<std::string> FixpointSafetyVisitor::ExtractHeadVariables(RelBindingsFormula& node) const {
  std::vector<std::string> head_vars;
  for (const auto& b : node.bindings) {
    if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
      head_vars.push_back(vb->id);
    }
  }
  return head_vars;
}

BoundSet FixpointSafetyVisitor::ExtractPlaceholderSafety(RelFullAppl& node, const BoundSet& placeholder_safety) {
  std::vector<std::string> call_variables;
  std::vector<size_t> variable_indices;

  for (size_t i = 0; i < node.params.size(); i++) {
    auto expr = node.params[i] ? node.params[i]->GetExpr() : nullptr;
    if (!expr) continue;
    expr->Accept(*this);
    auto* term_expr = dynamic_cast<RelTermExpr*>(expr.get());
    if (!term_expr || !term_expr->term || expr->variables.size() != 1) continue;

    call_variables.push_back(*expr->variables.begin());
    variable_indices.push_back(i);
  }

  if (placeholder_safety.IsEmpty()) {
    int arity = GetContainer()->GetArity(recursive_relation_);
    auto table_source = TableSource(current_placeholder_, static_cast<size_t>(arity));
    auto projection = Projection(variable_indices, table_source);
    Bound binding_bound{call_variables};
    binding_bound.Add(projection);
    return BoundSet({binding_bound});
  }

  std::unordered_set<Bound> renamed_bounds;
  for (const auto& bound : placeholder_safety.bounds) {
    std::unordered_map<std::string, std::string> rename_map;
    if (head_variables_.size() == call_variables.size()) {
      for (size_t i = 0; i < head_variables_.size() && i < call_variables.size(); ++i) {
        rename_map[head_variables_[i]] = call_variables[i];
      }
      renamed_bounds.insert(bound.Renamed(rename_map));
    }
  }
  return BoundSet(renamed_bounds);
}

BoundSet FixpointSafetyVisitor::ComputeFixpoint(RelBindingsFormula& node) {
  head_variables_ = ExtractHeadVariables(node);

  const int MAX_ITERATIONS = 100;
  BoundSet previous_safety;

  while (iteration_count_ < MAX_ITERATIONS) {
    if (node.formula) node.formula->Accept(*this);

    BoundSet formula_safety = node.formula ? node.formula->safety : BoundSet();

    if (iteration_count_ > 0 && BoundSetsEqual(formula_safety, previous_safety)) {
      return RemovePlaceholderDomains(formula_safety);
    }

    previous_safety = formula_safety;
    iteration_count_++;
    std::string next_placeholder = GetNextPlaceholder();
    placeholder_safety_[next_placeholder] = formula_safety;
    current_placeholder_ = next_placeholder;
  }

  throw TranslationException(
      "Fixpoint safety computation did not converge after " + std::to_string(MAX_ITERATIONS) + " iterations",
      ErrorCode::INTERNAL_ERROR, SourceLocation(0, 0));
}

void FixpointSafetyVisitor::Visit(RelFullAppl& node) {
  if (auto* id_base = dynamic_cast<RelIDApplBase*>(node.base.get())) {
    std::string id = id_base->id;

    if (id == recursive_relation_) {
      BoundSet ph;
      auto it = placeholder_safety_.find(current_placeholder_);
      if (it != placeholder_safety_.end()) ph = it->second;
      node.safety = ExtractPlaceholderSafety(node, ph);
      return;
    }
    if (IsPlaceholder(id)) {
      BoundSet ph;
      auto it = placeholder_safety_.find(id);
      if (it != placeholder_safety_.end()) ph = it->second;
      std::string saved = current_placeholder_;
      current_placeholder_ = id;
      node.safety = ExtractPlaceholderSafety(node, ph);
      current_placeholder_ = saved;
      return;
    }
  }

  SafetyVisitor::Visit(node);
}

bool FixpointSafetyVisitor::IsPlaceholder(const std::string& id) const {
  if (id.empty() || id[0] != 'R') return false;
  if (id.size() == 1) return false;
  for (size_t i = 1; i < id.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(id[i]))) return false;
  }
  return true;
}

std::string FixpointSafetyVisitor::GetNextPlaceholder() { return "R" + std::to_string(iteration_count_ + 1); }

bool FixpointSafetyVisitor::BoundSetsEqual(const BoundSet& a, const BoundSet& b) const { return a.bounds == b.bounds; }

BoundSet FixpointSafetyVisitor::RemovePlaceholderDomains(const BoundSet& safety) const {
  return safety.WithRemovedProjections([this](const Projection& projection) {
    auto resolved = ResolvePromisedSource(projection.source);
    auto table_source = std::dynamic_pointer_cast<const TableSource>(resolved);
    return table_source && IsPlaceholder(table_source->table_name);
  });
}

}  // namespace rel2sql
