#ifndef PREPROCESSING_SAFETY_INFERRER_H
#define PREPROCESSING_SAFETY_INFERRER_H

#include <memory>
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

  RelContextBuilder* container_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_SAFETY_INFERRER_H
