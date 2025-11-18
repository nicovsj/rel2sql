#include "structs/extended_ast.h"

#include <algorithm>
#include <queue>
#include <stdexcept>

#include "utils/extended_node_exceptions.h"

namespace rel2sql {

// RelAST constructors
RelAST::RelAST() : root_(nullptr) {}

RelAST::RelAST(antlr4::ParserRuleContext* root) : root_(root) {}

RelAST::RelAST(antlr4::ParserRuleContext* root, const rel2sql::EDBMap& edb_map) : root_(root) {
  for (const auto& [id, rel_info] : edb_map) {
    // Store the EDBInfo for later use
    relation_info_.emplace(id, rel_info);

    if (rel_info.HasCustomNamedAttributes()) {
      AddEDB(id, rel_info.attribute_names);
    } else {
      // For relations with standard A1, A2, etc. naming, use the standard AddEDB
      AddEDB(id, rel_info.arity);
    }
  }
}

// RelAST methods
std::shared_ptr<RelASTNode> RelAST::Root() const {
  auto it = index_.find(root_);
  if (it == index_.end()) {
    return nullptr;
  }
  return it->second;
}

std::shared_ptr<RelASTNode> RelAST::GetNode(antlr4::ParserRuleContext* ctx) {
  auto it = index_.find(ctx);
  if (it == index_.end()) {
    index_[ctx] = std::make_shared<RelASTNode>();
  }
  return index_[ctx];
}

int RelAST::GetArity(const std::string& id) const {
  auto rel_info = GetRelationInfo(id);
  if (!rel_info) {
    return 1;
  }
  return rel_info->arity;
}

antlr4::ParserRuleContext* RelAST::ParseTree() const { return root_; }

void RelAST::SetParseTree(antlr4::ParserRuleContext* root) { root_ = root; }

void RelAST::MarkAsIDB(const std::string& id) {
  ids_.insert(id);

  if (IsEDB(id)) throw std::runtime_error("IDB " + id + " already in the set of EDBs");

  vars_.erase(id);
  idb_.insert(id);

  // Create RelationInfo with placeholder arity (0), will be updated later
  if (relation_info_.find(id) == relation_info_.end()) {
    relation_info_.emplace(id, RelationInfo(0));
  }
}

void RelAST::AddIDB(const std::string& id, int arity) {
  ids_.insert(id);

  if (IsEDB(id)) throw std::runtime_error("IDB " + id + " already in the set of EDBs");

  vars_.erase(id);
  idb_.insert(id);

  // Update or create RelationInfo with the actual arity
  auto it = relation_info_.find(id);
  if (it != relation_info_.end()) {
    // Update existing RelationInfo (preserve dependencies)
    it->second.arity = arity;
    // Update attribute names if needed
    if (it->second.attribute_names.empty() || it->second.attribute_names.size() != static_cast<size_t>(arity)) {
      it->second.attribute_names.clear();
      it->second.attribute_names.reserve(arity);
      for (int i = 0; i < arity; ++i) {
        it->second.attribute_names.push_back("A" + std::to_string(i + 1));
      }
    }
  } else {
    relation_info_.emplace(id, RelationInfo(arity));
  }
}

void RelAST::AddEDB(const std::string& edb, int arity) {
  if (IsIDB(edb)) throw std::runtime_error("EDB " + edb + " already in the set of IDBs");
  if (IsVar(edb)) throw std::runtime_error("EDB " + edb + " already in the set of variables");

  ids_.insert(edb);
  edb_.insert(edb);

  relation_info_.emplace(edb, RelationInfo(arity));
}

void RelAST::AddEDB(const std::string& edb, const std::vector<std::string>& attribute_names) {
  if (idb_.find(edb) != idb_.end()) {
    throw std::runtime_error("EDB " + edb + " already in the set of IDBs");
  }

  if (vars_.find(edb) != vars_.end()) {
    throw std::runtime_error("EDB " + edb + " already in the set of variables");
  }

  ids_.insert(edb);
  edb_.insert(edb);

  relation_info_.emplace(edb, RelationInfo(std::move(attribute_names)));
}

void RelAST::AddVar(const std::string& var) {
  if (idb_.find(var) != idb_.end() || edb_.find(var) != edb_.end()) {
    // ID is already in the set of IDBs or EDBs then do nothing
    return;
  }

  ids_.insert(var);
  vars_.insert(var);
}

void RelAST::AddDependency(const std::string& id, const std::string& dep) { relation_info_[id].AddDependency(dep); }

std::optional<RelationInfo> RelAST::GetRelationInfo(const std::string& edb) const {
  auto it = relation_info_.find(edb);
  if (it != relation_info_.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool RelAST::IsIDB(const std::string& id) const { return idb_.find(id) != idb_.end(); }

bool RelAST::IsEDB(const std::string& id) const { return edb_.find(id) != edb_.end(); }

bool RelAST::IsVar(const std::string& var) const { return vars_.find(var) != vars_.end(); }

bool RelAST::IsID(const std::string& id) const { return ids_.find(id) != ids_.end(); }

const std::vector<std::string>& RelAST::SortedIDs() const { return sorted_ids_; }

void RelAST::RemoveVarsFromDependencyGraph() {
  for (const auto& id : ids_) {
    if (IsVar(id)) {
      // Remove this ID from the dependency graph
      relation_info_.erase(id);
      continue;
    }
    // Remove variable dependencies from this ID's dependencies
    auto& deps = relation_info_[id].dependencies;
    deps.erase(std::remove_if(deps.begin(), deps.end(),
                              [this](const std::string& dep) { return IsVar(dep); }),
               deps.end());
  }
}

void RelAST::ComputeTopologicalSort() {
  // Build the dependency graph from relation_info_
  std::unordered_map<std::string, std::vector<std::string>> graph;
  for (const auto& [id, info] : relation_info_) {
    if (!IsVar(id)) {  // Only include non-variable IDs
      graph[id] = info.dependencies;
    }
  }

  // Compute in-degrees
  std::unordered_map<std::string, int> in_degree;
  for (const auto& [id, deps] : graph) {
    in_degree[id] = 0;
  }

  for (const auto& [id, deps] : graph) {
    for (const auto& dep : deps) {
      if (graph.find(dep) != graph.end()) {  // Only count if dep is in graph
        in_degree[dep]++;
      }
    }
  }

  // Kahn's algorithm for topological sort
  std::queue<std::string> q;
  for (const auto& [id, degree] : in_degree) {
    if (degree == 0) {
      q.push(id);
    }
  }

  std::vector<std::string> order;
  while (!q.empty()) {
    std::string id = q.front();
    q.pop();
    order.push_back(id);

    for (const auto& dep : graph[id]) {
      if (graph.find(dep) != graph.end()) {  // Only process if dep is in graph
        in_degree[dep]--;
        if (in_degree[dep] == 0) {
          q.push(dep);
        }
      }
    }
  }

  // Reverse to get inverse topological order (dependencies before dependents)
  std::reverse(order.begin(), order.end());
  sorted_ids_ = std::move(order);
}

bool operator==(const RelASTNode& lhs, const RelASTNode& rhs) {
  // Compare variables
  if (lhs.variables != rhs.variables) {
    throw ExtendedNodeDifferenceException(nullptr, "variables", "Variable sets differ");
  }

  // Compare free_variables
  if (lhs.free_variables != rhs.free_variables) {
    throw ExtendedNodeDifferenceException(nullptr, "free_variables", "Free variable sets differ");
  }

  // Compare disabled flag
  if (lhs.disabled != rhs.disabled) {
    throw ExtendedNodeDifferenceException(
        nullptr, "disabled",
        "Disabled flag differs: " + std::to_string(lhs.disabled) + " vs " + std::to_string(rhs.disabled));
  }

  // Compare has_only_literal_values
  if (lhs.has_only_literal_values != rhs.has_only_literal_values) {
    throw ExtendedNodeDifferenceException(
        nullptr, "has_only_literal_values",
        "has_only_literal_values flag differs: " + std::to_string(lhs.has_only_literal_values) + " vs " +
            std::to_string(rhs.has_only_literal_values));
  }

  // Compare is_recursive
  if (lhs.is_recursive != rhs.is_recursive) {
    throw ExtendedNodeDifferenceException(
        nullptr, "is_recursive",
        "is_recursive flag differs: " + std::to_string(lhs.is_recursive) + " vs " + std::to_string(rhs.is_recursive));
  }

  // Compare arity
  if (lhs.arity != rhs.arity) {
    throw ExtendedNodeDifferenceException(
        nullptr, "arity", "Arity differs: " + std::to_string(lhs.arity) + " vs " + std::to_string(rhs.arity));
  }

  // Compare constant
  if (lhs.constant != rhs.constant) {
    throw ExtendedNodeDifferenceException(nullptr, "constant", "Constant values differ");
  }

  // Compare safeness
  if (lhs.safeness != rhs.safeness) {
    throw ExtendedNodeDifferenceException(nullptr, "safeness", "Safeness sets differ");
  }

  // Compare multiple_defs (by pointer equality since they're ParserRuleContext pointers)
  if (lhs.multiple_defs.size() != rhs.multiple_defs.size()) {
    throw ExtendedNodeDifferenceException(nullptr, "multiple_defs",
                                          "multiple_defs size differs: " + std::to_string(lhs.multiple_defs.size()) +
                                              " vs " + std::to_string(rhs.multiple_defs.size()));
  }
  for (size_t i = 0; i < lhs.multiple_defs.size(); i++) {
    if (lhs.multiple_defs[i] != rhs.multiple_defs[i]) {
      throw ExtendedNodeDifferenceException(nullptr, "multiple_defs",
                                            "multiple_defs[" + std::to_string(i) + "] differs");
    }
  }

  // Compare comparator_formulas
  if (lhs.comparator_formulas.size() != rhs.comparator_formulas.size()) {
    throw ExtendedNodeDifferenceException(
        nullptr, "comparator_formulas",
        "comparator_formulas size differs: " + std::to_string(lhs.comparator_formulas.size()) + " vs " +
            std::to_string(rhs.comparator_formulas.size()));
  }
  for (size_t i = 0; i < lhs.comparator_formulas.size(); i++) {
    if (lhs.comparator_formulas[i] != rhs.comparator_formulas[i]) {
      throw ExtendedNodeDifferenceException(nullptr, "comparator_formulas",
                                            "comparator_formulas[" + std::to_string(i) + "] differs");
    }
  }

  // Compare other_formulas
  if (lhs.other_formulas.size() != rhs.other_formulas.size()) {
    throw ExtendedNodeDifferenceException(nullptr, "other_formulas",
                                          "other_formulas size differs: " + std::to_string(lhs.other_formulas.size()) +
                                              " vs " + std::to_string(rhs.other_formulas.size()));
  }
  for (size_t i = 0; i < lhs.other_formulas.size(); i++) {
    if (lhs.other_formulas[i] != rhs.other_formulas[i]) {
      throw ExtendedNodeDifferenceException(nullptr, "other_formulas",
                                            "other_formulas[" + std::to_string(i) + "] differs");
    }
  }

  // Recursively compare children
  if (lhs.children.size() != rhs.children.size()) {
    throw ExtendedNodeDifferenceException(nullptr, "children",
                                          "Children count differs: " + std::to_string(lhs.children.size()) + " vs " +
                                              std::to_string(rhs.children.size()));
  }

  for (size_t i = 0; i < lhs.children.size(); i++) {
    if (!lhs.children[i] || !rhs.children[i]) {
      if (lhs.children[i] != rhs.children[i]) {
        throw ExtendedNodeDifferenceException(nullptr, "children",
                                              "children[" + std::to_string(i) + "] is null in one but not the other");
      }
      continue;
    }

    // Recursively compare child nodes
    try {
      if (!(*lhs.children[i] == *rhs.children[i])) {
        throw ExtendedNodeDifferenceException(nullptr, "children", "children[" + std::to_string(i) + "] differ");
      }
    } catch (const ExtendedNodeDifferenceException& e) {
      // Re-throw with additional context about which child differs
      throw ExtendedNodeDifferenceException(nullptr, "children[" + std::to_string(i) + "]." + e.GetFieldName(),
                                            e.GetDetails());
    }
  }

  // Note: We don't compare sql_expression as it's not part of the ExtendedAST structure itself
  // and may differ even when the ExtendedAST is equivalent

  return true;
}

bool operator!=(const RelASTNode& lhs, const RelASTNode& rhs) {
  try {
    return !(lhs == rhs);
  } catch (const ExtendedNodeDifferenceException&) {
    // If they differ, they're not equal
    return true;
  }
}

bool AreExtendedASTsEqual(const RelAST& ast1, const RelAST& ast2) {
  auto root1 = ast1.Root();
  auto root2 = ast2.Root();

  if (!root1 && !root2) return true;
  if (!root1 || !root2) {
    throw ExtendedNodeDifferenceException(nullptr, "root", "One AST has a root node and the other doesn't");
  }

  try {
    return *root1 == *root2;
  } catch (const ExtendedNodeDifferenceException& e) {
    // Re-throw with context that this is at the root level
    throw ExtendedNodeDifferenceException(ast1.ParseTree(), "root." + e.GetFieldName(), e.GetDetails());
  }
}

}  // namespace rel2sql
