#include "fixpoint_safety_visitor.h"

#include <cctype>

#include "rel_ast/extended_ast.h"
#include "support/exceptions.h"

namespace rel2sql {

FixpointSafetyVisitor::FixpointSafetyVisitor(std::shared_ptr<RelAST> ast, const std::string& recursive_relation)
    : SafeVisitor(ast),
      recursive_relation_(recursive_relation),
      iteration_count_(0) {
  // Don't initialize R0 - it will be treated as a normal relation call on first iteration
  current_placeholder_ = "R0";
}

BoundSet FixpointSafetyVisitor::ExtractPlaceholderSafety(psr::FullApplContext* ctx,
                                                          const BoundSet& placeholder_safety) {
  // Extract variable names and indices from the call
  std::vector<std::string> call_variables;
  std::vector<size_t> variable_indices;

  for (size_t i = 0; i < ctx->applParams()->applParam().size(); i++) {
    auto param = ctx->applParams()->applParam()[i];
    visit(param);
    auto node = GetNode(param);

    if (!dynamic_cast<psr::IDExprContext*>(param->expr())) continue;
    if (node->variables.size() != 1) continue;

    auto variable = *node->variables.begin();
    call_variables.push_back(variable);
    variable_indices.push_back(i);
  }

  // If placeholder safety is empty (like R0 initially), treat it as a normal relation call
  // Create a bound with TableSource for the placeholder
  if (placeholder_safety.IsEmpty()) {
    auto arity = ast_->GetArity(recursive_relation_);
    auto table_source = TableSource(current_placeholder_, arity);
    auto projection = Projection(variable_indices, table_source);
    Bound binding_bound{call_variables};
    binding_bound.Add(projection);
    return BoundSet({binding_bound});
  }

  // The placeholder safety represents the safety of the relation's head variables.
  // The head variables are stored in head_variables_ in order (e.g., [y, x] for def S {(y,x) : ...}).
  // We need to rename the placeholder safety variables to match the actual call variables.
  // The rename map: head_variables_[i] → call_variables[i]

  std::unordered_set<Bound> renamed_bounds;

  for (const auto& bound : placeholder_safety.bounds) {
    // Create a rename map: map head variable at position i to call variable at position i
    // Example: if head_variables_ = [y, x] and call_variables = [z, y]
    //          then rename_map = {y → z, x → y}
    std::unordered_map<std::string, std::string> rename_map;

    // The placeholder safety bounds use head_variables_ in order
    // We map each head variable to the corresponding call variable by position
    if (head_variables_.size() == call_variables.size()) {
      for (size_t i = 0; i < head_variables_.size() && i < call_variables.size(); ++i) {
        rename_map[head_variables_[i]] = call_variables[i];
      }
      renamed_bounds.insert(bound.Renamed(rename_map));
    } else {
      // Mismatch in arity - this shouldn't happen, but return empty to be safe
      return BoundSet();
    }
  }

  return BoundSet(renamed_bounds);
}

std::vector<std::string> FixpointSafetyVisitor::ExtractHeadVariables(psr::BindingsFormulaContext* ctx) const {
  std::vector<std::string> head_vars;
  if (ctx->bindingInner()) {
    for (auto* binding_ctx : ctx->bindingInner()->binding()) {
      if (binding_ctx->id) {
        head_vars.push_back(binding_ctx->id->getText());
      }
    }
  }
  return head_vars;
}

BoundSet FixpointSafetyVisitor::ComputeFixpoint(psr::BindingsFormulaContext* ctx) {
  // Extract and store head variables in order (e.g., [y, x] for def S {(y,x) : ...})
  head_variables_ = ExtractHeadVariables(ctx);

  const int MAX_ITERATIONS = 100;
  BoundSet previous_safety;

  while (iteration_count_ < MAX_ITERATIONS) {
    // Visit the formula with current placeholder
    visit(ctx->formula());

    // Get the computed safety from the formula
    auto formula_safety = GetNode(ctx->formula())->safety;

    // The formula safety contains bounds mentioning variables from the formula (e.g., x, y, z).
    // The placeholder safety should represent the safety of the relation's head variables.
    // We use the formula safety as-is - it already contains bounds for the head variables.
    // The ExtractPlaceholderSafety method will correctly map head_variables_[i] to call_variables[i]
    // when using this placeholder safety in a recursive call.

    // Check for fixpoint: compare with previous iteration
    if (iteration_count_ > 0 && BoundSetsEqual(formula_safety, previous_safety)) {
      // Fixpoint reached! Remove placeholder domains before returning
      return RemovePlaceholderDomains(formula_safety);
    }

    // Store as next placeholder and continue
    previous_safety = formula_safety;
    iteration_count_++;
    std::string next_placeholder = GetNextPlaceholder();
    placeholder_safety_[next_placeholder] = formula_safety;
    current_placeholder_ = next_placeholder;
  }

  // Max iterations reached without convergence
  SourceLocation location = GetSourceLocation(ctx);
  throw TranslationException("Fixpoint safety computation did not converge after " +
                                 std::to_string(MAX_ITERATIONS) + " iterations",
                             ErrorCode::INTERNAL_ERROR, location);
}

std::any FixpointSafetyVisitor::visitFullAppl(psr::FullApplContext* ctx) {
  visit(ctx->applBase());

  // Base is an ID
  if (ctx->applBase()->T_ID()) {
    std::string id = ctx->applBase()->T_ID()->getText();

    // Check if this is a call to the recursive relation
    if (id == recursive_relation_) {
      // Get placeholder safety (may be empty for R0, which we handle in ExtractPlaceholderSafety)
      auto it = placeholder_safety_.find(current_placeholder_);
      BoundSet placeholder_safety;
      if (it != placeholder_safety_.end()) {
        placeholder_safety = it->second;
      }
      auto node = GetNode(ctx);
      node->safety = ExtractPlaceholderSafety(ctx, placeholder_safety);
      return {};
    } else if (IsPlaceholder(id)) {
      // This shouldn't happen in normal flow, but handle it
      auto it = placeholder_safety_.find(id);
      BoundSet placeholder_safety;
      if (it != placeholder_safety_.end()) {
        placeholder_safety = it->second;
      }
      // Temporarily set current_placeholder_ to use the right placeholder name
      std::string saved_placeholder = current_placeholder_;
      current_placeholder_ = id;
      auto node = GetNode(ctx);
      node->safety = ExtractPlaceholderSafety(ctx, placeholder_safety);
      current_placeholder_ = saved_placeholder;
      return {};
    }
  }

  // For non-recursive calls, delegate to parent SafeVisitor
  return SafeVisitor::visitFullAppl(ctx);
}

bool FixpointSafetyVisitor::IsPlaceholder(const std::string& id) const {
  if (id.empty() || id[0] != 'R') return false;
  if (id.size() == 1) return false;  // Just "R" is not a placeholder
  // Check if the rest is numeric
  for (size_t i = 1; i < id.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(id[i]))) return false;
  }
  return true;
}

std::string FixpointSafetyVisitor::GetNextPlaceholder() {
  return "R" + std::to_string(iteration_count_ + 1);
}

bool FixpointSafetyVisitor::BoundSetsEqual(const BoundSet& a, const BoundSet& b) const {
  // Compare the unordered_sets directly
  return a.bounds == b.bounds;
}

BoundSet FixpointSafetyVisitor::RemovePlaceholderDomains(const BoundSet& safety) const {
  // Use BoundSet's WithRemovedProjections method with a lambda that identifies placeholder projections
  return safety.WithRemovedProjections([this](const Projection& projection) {
    auto resolved_source = ResolvePromisedSource(projection.source);
    auto table_source = std::dynamic_pointer_cast<const TableSource>(resolved_source);

    // Remove projections whose source is a placeholder TableSource
    return table_source && IsPlaceholder(table_source->table_name);
  });
}

}  // namespace rel2sql
