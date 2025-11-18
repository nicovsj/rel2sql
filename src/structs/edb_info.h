#ifndef REL2SQL_EDB_INFO_H
#define REL2SQL_EDB_INFO_H

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
namespace rel2sql {

/**
 * Represents information about an ID relation.
 *
 * This structure can handle both named and unnamed ID attributes:
 * - If attribute_names is provided, it contains the ordered list of attribute names
 * - If attribute_names is empty, the EDB uses default A1, A2, A3... naming
 * - The arity is always inferred from the attribute_names size when provided
 */
struct RelationInfo {
  std::vector<std::string> attribute_names;
  int arity;
  std::vector<std::string> dependencies;

  // Default constructor for unnamed relations (uses A1, A2, A3...)
  RelationInfo() = default;

  // Constructor for unnamed relations with explicit arity - auto-populates A1, A2, A3...
  explicit RelationInfo(int arity) : arity(arity) {
    attribute_names.reserve(arity);
    for (int i = 0; i < arity; ++i) {
      attribute_names.push_back("A" + std::to_string(i + 1));
    }
  }

  // Constructor for named EDBs
  explicit RelationInfo(std::vector<std::string> names) : attribute_names(std::move(names)), arity(attribute_names.size()) {}

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

struct EDBMap {
  using iterator = std::unordered_map<std::string, RelationInfo>::iterator;
  using const_iterator = std::unordered_map<std::string, RelationInfo>::const_iterator;

  std::unordered_map<std::string, RelationInfo> map;

  EDBMap() = default;

  EDBMap(const std::unordered_map<std::string, RelationInfo>& map) : map(map) {}

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

// Helper functions for EDBMap
namespace edb_utils {
// Convert from old external_arity_map format to EDBMap
inline EDBMap FromArityMap(const std::unordered_map<std::string, int>& arity_map) {
  EDBMap edb_map;
  for (const auto& [name, arity] : arity_map) {
    edb_map[name] = RelationInfo(arity);  // Create unnamed EDB with explicit arity
  }
  return edb_map;
}

// Create EDBMap with named attributes
inline EDBMap WithNamedAttributes(const std::unordered_map<std::string, std::vector<std::string>>& named_map) {
  EDBMap edb_map;
  for (const auto& [name, attributes] : named_map) {
    edb_map[name] = RelationInfo(attributes);
  }
  return edb_map;
}

}  // namespace edb_utils

}  // namespace rel2sql

#endif  // REL2SQL_EDB_INFO_H
