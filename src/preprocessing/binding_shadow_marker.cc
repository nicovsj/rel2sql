#include "preprocessing/binding_shadow_marker.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace rel2sql {

namespace {

// Bound names in scope, counted so a nested binder rebinding the same name doesn't drop it early.
using Scope = std::unordered_map<std::string, int>;

const std::vector<std::shared_ptr<RelBinding>>* BindingsOf(RelNode* node) {
  if (auto* n = dynamic_cast<RelExprAbstraction*>(node)) return &n->bindings;
  if (auto* n = dynamic_cast<RelFormulaAbstraction*>(node)) return &n->bindings;
  if (auto* n = dynamic_cast<RelExistential*>(node)) return &n->bindings;
  if (auto* n = dynamic_cast<RelUniversal*>(node)) return &n->bindings;
  return nullptr;
}

void Walk(const std::shared_ptr<RelNode>& node, const RelContextBuilder& builder, Scope& scope) {
  if (!node) return;

  if (auto* id = dynamic_cast<RelIDTerm*>(node.get())) {
    if (scope.count(id->id) && builder.IsRelation(id->id)) id->shadows_relation = true;
    return;
  }

  std::vector<std::string> introduced;
  if (const auto* bindings = BindingsOf(node.get())) {
    for (const auto& b : *bindings) {
      if (auto* vb = dynamic_cast<RelVarBinding*>(b.get())) {
        ++scope[vb->id];
        introduced.push_back(vb->id);
      }
    }
  }

  for (const auto& child : node->Children()) Walk(child, builder, scope);

  for (const auto& name : introduced) {
    if (--scope[name] == 0) scope.erase(name);
  }
}

}  // namespace

void MarkBindingShadowedIds(const std::shared_ptr<RelNode>& root, const RelContextBuilder& builder) {
  Scope scope;
  Walk(root, builder, scope);
}

}  // namespace rel2sql
