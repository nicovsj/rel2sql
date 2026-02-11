#ifndef REL2SQL_EDB_INFO_H
#define REL2SQL_EDB_INFO_H

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace rel2sql {

struct RelAbstraction;
class RelNode;

struct RecursiveBranchInfoTyped {
  std::shared_ptr<RelNode> exists_clause;
  std::shared_ptr<RelNode> recursive_call;
  std::shared_ptr<RelNode> residual_formula;
};

struct RecursionInfoTyped {
  std::vector<std::shared_ptr<RelAbstraction>> non_recursive_disjuncts;
  std::vector<RecursiveBranchInfoTyped> recursive_disjuncts;

  bool empty() const {
    return non_recursive_disjuncts.empty() && recursive_disjuncts.empty();
  }
};

struct RelationInfo {
  std::vector<std::string> attribute_names;
  int arity;
  std::vector<std::string> dependencies;

  RelationInfo() = default;

  // Constructor for unnamed relations with explicit arity - auto-populates A1, A2, A3...
  explicit RelationInfo(int arity) : arity(arity) {
    attribute_names.reserve(arity);
    for (int i = 0; i < arity; ++i) {
      attribute_names.push_back("A" + std::to_string(i + 1));
    }
  }

  // Constructor for named EDBs
  explicit RelationInfo(std::vector<std::string> names)
      : attribute_names(std::move(names)), arity(attribute_names.size()) {}

  int Arity() const { return arity; }

  bool HasNamedAttributes() const { return !attribute_names.empty(); }

  // Check if this EDB has custom named attributes (vs auto-generated A1, A2, etc.)
  bool HasCustomNamedAttributes() const {
    if (attribute_names.empty()) return false;

    // Check if all attributes follow the A1, A2, A3... pattern
    for (size_t i = 0; i < attribute_names.size(); ++i) {
      if (attribute_names[i] != "A" + std::to_string(i + 1)) {
        return true;  // Found a custom name
      }
    }
    return false;  // All names follow the standard pattern
  }

  // Get attribute name at position i (0-based)
  std::string AttributeName(int i) const {
    if (i < 0 || i >= static_cast<int>(attribute_names.size())) {
      throw std::runtime_error("Attribute index out of range");
    }
    return attribute_names[i];
  }

  void AddDependency(const std::string& id) { dependencies.push_back(id); }
};

struct RelationInfoTyped {
  std::vector<std::string> attribute_names;
  int arity;
  std::vector<std::string> dependencies;
  RecursionInfoTyped recursion_metadata;

  explicit RelationInfoTyped(int arity = 0) : arity(arity) {
    attribute_names.reserve(arity);
    for (int i = 0; i < arity; ++i) {
      attribute_names.push_back("A" + std::to_string(i + 1));
    }
  }

  explicit RelationInfoTyped(std::vector<std::string> names)
      : attribute_names(std::move(names)), arity(static_cast<int>(attribute_names.size())) {}

  void AddDependency(const std::string& id) { dependencies.push_back(id); }

  void AddNonRecursiveDisjunct(const std::shared_ptr<RelAbstraction>& node) {
    recursion_metadata.non_recursive_disjuncts.push_back(node);
  }

  void AddRecursiveDisjunct(const RecursiveBranchInfoTyped& info) {
    recursion_metadata.recursive_disjuncts.push_back(info);
  }
};

struct RelationMap {
  using iterator = std::unordered_map<std::string, RelationInfo>::iterator;
  using const_iterator = std::unordered_map<std::string, RelationInfo>::const_iterator;

  std::unordered_map<std::string, RelationInfo> map;

  RelationMap() = default;

  RelationMap(const std::unordered_map<std::string, RelationInfo>& map) : map(map) {}

  size_t size() const { return map.size(); }

  bool has(const std::string& name) const { return map.find(name) != map.end(); }

  RelationInfo get(const std::string& name) const { return map.at(name); }

  void set(const std::string& name, const RelationInfo& info) { map[name] = info; }

  const_iterator find(const std::string& name) const { return map.find(name); }

  iterator find(const std::string& name) { return map.find(name); }

  // Subscript operators
  RelationInfo& operator[](const std::string& name) { return map[name]; }

  const RelationInfo& operator[](const std::string& name) const { return map.at(name); }

  // Iterators
  auto begin() { return map.begin(); }
  auto end() { return map.end(); }
  auto begin() const { return map.begin(); }
  auto end() const { return map.end(); }
};

// Helper functions for RelationMap
namespace relation_map {
// Convert from old external_arity_map format to RelationMap
inline RelationMap FromArityMap(const std::unordered_map<std::string, int>& arity_map) {
  RelationMap edb_map;
  for (const auto& [name, arity] : arity_map) {
    edb_map[name] = RelationInfo(arity);  // Create unnamed EDB with explicit arity
  }
  return edb_map;
}

// Create RelationMap with named attributes
inline RelationMap WithNamedAttributes(const std::unordered_map<std::string, std::vector<std::string>>& named_map) {
  RelationMap edb_map;
  for (const auto& [name, attributes] : named_map) {
    edb_map[name] = RelationInfo(attributes);
  }
  return edb_map;
}

}  // namespace relation_map

}  // namespace rel2sql

#endif  // REL2SQL_EDB_INFO_H
