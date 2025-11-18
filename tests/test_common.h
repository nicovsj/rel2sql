#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "rel_ast/edb_info.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {

// Creates the default EDB map used in most tests
inline EDBMap CreateDefaultEDBMap() {
  EDBMap edb_map;
  edb_map["A"] = RelationInfo(1);
  edb_map["B"] = RelationInfo(2);
  edb_map["C"] = RelationInfo(3);
  edb_map["D"] = RelationInfo(1);
  edb_map["E"] = RelationInfo(2);
  edb_map["F"] = RelationInfo(3);
  edb_map["G"] = RelationInfo(1);
  edb_map["H"] = RelationInfo(2);
  edb_map["I"] = RelationInfo(3);
  return edb_map;
}

// Helper function to compare two sql::ast::Expression structures
// Uses deep equality comparison via operator== overloads
// Returns true if the ASTs are structurally equivalent
inline bool AreASTsEqual(const std::shared_ptr<sql::ast::Expression>& ast1,
                         const std::shared_ptr<sql::ast::Expression>& ast2) {
  if (!ast1 && !ast2) return true;
  if (!ast1 || !ast2) return false;
  return *ast1 == *ast2;
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
