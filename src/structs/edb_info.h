#ifndef REL2SQL_EDB_INFO_H
#define REL2SQL_EDB_INFO_H

#include <string>
#include <unordered_map>
#include <vector>

namespace rel2sql {

/**
 * Represents information about an External Database (EDB) relation.
 *
 * This structure can handle both named and unnamed EDB attributes:
 * - If attribute_names is provided, it contains the ordered list of attribute names
 * - If attribute_names is empty, the EDB uses default A1, A2, A3... naming
 * - The arity is always inferred from the attribute_names size when provided
 */
struct EDBInfo {
  std::vector<std::string> attribute_names;

  // Default constructor for unnamed EDBs (uses A1, A2, A3...)
  EDBInfo() = default;

  // Constructor for unnamed EDBs with explicit arity - auto-populates A1, A2, A3...
  explicit EDBInfo(int arity) {
    attribute_names.reserve(arity);
    for (int i = 0; i < arity; ++i) {
      attribute_names.push_back("A" + std::to_string(i + 1));
    }
  }

  // Constructor for named EDBs
  explicit EDBInfo(std::vector<std::string> names) : attribute_names(std::move(names)) {}

  // Get the arity (number of attributes)
  int arity() const { return static_cast<int>(attribute_names.size()); }

  // Check if this EDB has custom named attributes (vs auto-generated A1, A2, etc.)
  bool has_custom_named_attributes() const {
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
  std::string get_attribute_name(int i) const {
    if (i < 0 || i >= static_cast<int>(attribute_names.size())) {
      throw std::runtime_error("Attribute index out of range");
    }
    return attribute_names[i];
  }
};

// Type alias for EDB map
using EDBMap = std::unordered_map<std::string, EDBInfo>;

// Helper functions for EDBMap
namespace edb_utils {
// Convert from old external_arity_map format to EDBMap
inline EDBMap FromArityMap(const std::unordered_map<std::string, int>& arity_map) {
  EDBMap edb_map;
  for (const auto& [name, arity] : arity_map) {
    edb_map[name] = EDBInfo(arity);  // Create unnamed EDB with explicit arity
  }
  return edb_map;
}

// Create EDBMap with named attributes
inline EDBMap WithNamedAttributes(const std::unordered_map<std::string, std::vector<std::string>>& named_map) {
  EDBMap edb_map;
  for (const auto& [name, attributes] : named_map) {
    edb_map[name] = EDBInfo(attributes);
  }
  return edb_map;
}
}  // namespace edb_utils

}  // namespace rel2sql

#endif  // REL2SQL_EDB_INFO_H
