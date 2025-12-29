#ifndef FIXPOINT_SAFETY_VISITOR_H
#define FIXPOINT_SAFETY_VISITOR_H

#include <antlr4-runtime.h>
#include <string>
#include <unordered_map>

#include "preprocessing/safe_visitor.h"
#include "rel_ast/extended_ast.h"

namespace rel2sql {

class FixpointSafetyVisitor : public SafeVisitor {
  /*
   * Visitor that computes safety for recursive definitions using a fixpoint algorithm.
   * Iteratively replaces recursive calls with placeholder relations until convergence.
   * Inherits from SafeVisitor to reuse all safety computation logic.
   */
 public:
  FixpointSafetyVisitor(std::shared_ptr<RelAST> ast, const std::string& recursive_relation);

  // Main entry point: computes fixpoint safety for a recursive bindings formula
  BoundSet ComputeFixpoint(psr::BindingsFormulaContext* ctx);

  // Override visitFullAppl to handle placeholder substitution for recursive calls
  std::any visitFullAppl(psr::FullApplContext* ctx) override;

 private:
  // Helper to check if an ID is a placeholder (e.g., "R0", "R1")
  bool IsPlaceholder(const std::string& id) const;

  // Generate next placeholder name (R0, R1, R2, ...)
  std::string GetNextPlaceholder();

  // Compare two BoundSets for equality (for fixpoint detection)
  bool BoundSetsEqual(const BoundSet& a, const BoundSet& b) const;

  // Extract variables from a FullAppl call to the recursive relation
  BoundSet ExtractPlaceholderSafety(psr::FullApplContext* ctx, const BoundSet& placeholder_safety);

  // Extract head variables from BindingsFormulaContext in order
  std::vector<std::string> ExtractHeadVariables(psr::BindingsFormulaContext* ctx) const;

  // Remove placeholder domains (R0, R1, R2, ...) from a BoundSet
  // Returns a new BoundSet with placeholder domains removed. Bounds with no remaining domains are removed.
  BoundSet RemovePlaceholderDomains(const BoundSet& safety) const;

  std::unordered_map<std::string, BoundSet> placeholder_safety_;
  std::string current_placeholder_;
  std::string recursive_relation_;
  std::vector<std::string> head_variables_;  // Ordered head variables (e.g., [y, x] for def S {(y,x) : ...})
  int iteration_count_;
};

}  // namespace rel2sql

#endif  // FIXPOINT_SAFETY_VISITOR_H
