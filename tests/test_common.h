#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "structs/edb_info.h"
#include "structs/sql_ast.h"

namespace rel2sql {

// Creates the default EDB map used in most tests
inline EDBMap CreateDefaultEDBMap() {
  EDBMap edb_map;
  edb_map["A"] = EDBInfo(1);
  edb_map["B"] = EDBInfo(2);
  edb_map["C"] = EDBInfo(3);
  edb_map["D"] = EDBInfo(1);
  edb_map["E"] = EDBInfo(2);
  edb_map["F"] = EDBInfo(3);
  edb_map["G"] = EDBInfo(1);
  edb_map["H"] = EDBInfo(2);
  edb_map["I"] = EDBInfo(3);
  return edb_map;
}

// Helper function to compare two sql::ast::Expression structures
// Currently uses ToString() comparison for simplicity
// Returns true if the ASTs are equivalent (same string representation)
inline bool AreASTsEqual(const std::shared_ptr<sql::ast::Expression>& ast1,
                         const std::shared_ptr<sql::ast::Expression>& ast2) {
  if (!ast1 && !ast2) return true;
  if (!ast1 || !ast2) return false;
  return ast1->ToString() == ast2->ToString();
}

// Helper function to normalize SQL strings for comparison
// This can be extended to handle formatting differences that don't affect semantics
inline std::string NormalizeSQL(const std::string& sql) {
  // For now, just return as-is. Can be enhanced to:
  // - Remove extra whitespace
  // - Normalize alias names
  // - Handle case sensitivity
  return sql;
}

}  // namespace rel2sql

#endif  // TEST_COMMON_H
