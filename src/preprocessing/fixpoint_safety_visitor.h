#ifndef PREPROCESSING_FIXPOINT_SAFETY_VISITOR_REL_H
#define PREPROCESSING_FIXPOINT_SAFETY_VISITOR_REL_H

#include <string>
#include <unordered_map>

#include "preprocessing/safety_visitor.h"
#include "rel_ast/bound_set.h"
#include "rel_ast/rel_ast.h"

namespace rel2sql {

/**
 * Computes safety for recursive definitions using a fixpoint algorithm.
 * Iteratively replaces recursive calls with placeholder relations until convergence.
 */
class FixpointSafetyVisitor : public SafetyVisitor {
 public:
  FixpointSafetyVisitor(RelASTContainer* container, const std::string& recursive_relation);

  BoundSet ComputeFixpoint(RelBindingsFormula& node);

  void Visit(RelFullAppl& node) override;

 private:
  bool IsPlaceholder(const std::string& id) const;
  std::string GetNextPlaceholder();
  bool BoundSetsEqual(const BoundSet& a, const BoundSet& b) const;
  BoundSet ExtractPlaceholderSafety(RelFullAppl& node, const BoundSet& placeholder_safety);
  std::vector<std::string> ExtractHeadVariables(RelBindingsFormula& node) const;
  BoundSet RemovePlaceholderDomains(const BoundSet& safety) const;

  std::unordered_map<std::string, BoundSet> placeholder_safety_;
  std::string current_placeholder_;
  std::string recursive_relation_;
  std::vector<std::string> head_variables_;
  int iteration_count_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_FIXPOINT_SAFETY_VISITOR_REL_H
