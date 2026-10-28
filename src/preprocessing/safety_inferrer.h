#ifndef PREPROCESSING_SAFETY_INFERRER_H
#define PREPROCESSING_SAFETY_INFERRER_H

#include <memory>
#include <unordered_map>
#include <vector>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_context_builder.h"

namespace rel2sql {

class SafetyInferrer {
 public:
  explicit SafetyInferrer(RelContextBuilder* container);
  void Run(std::shared_ptr<RelNode> root);

 private:
  void ComputeSafetyFromChildren(const std::shared_ptr<RelNode>& node);
  void InheritSafetyToChildren(RelNode* node);
  std::vector<std::shared_ptr<RelNode>> CollectNodesPostOrder(std::shared_ptr<RelNode> root);
  void RunSafetyFixpoint(const std::vector<std::shared_ptr<RelNode>>& nodes);
  // Bounds the recursive calls of each linearly recursive definition by an inductive invariant of its relation,
  // and leaves the safety of `nodes` computed under those bounds.
  void ComputeRecursiveInvariants(const std::shared_ptr<RelNode>& root,
                                  const std::vector<std::shared_ptr<RelNode>>& nodes);

  RelContextBuilder* container_;
  // Bounds of each recursive call inside its own definition, used in place of the bound by the relation itself.
  std::unordered_map<const RelNode*, BoundSet> recursive_call_bounds_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_SAFETY_INFERRER_H
