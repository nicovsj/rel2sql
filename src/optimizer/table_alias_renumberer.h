#ifndef TABLE_ALIAS_RENUMBERER_H
#define TABLE_ALIAS_RENUMBERER_H

#include <string>
#include <unordered_map>

#include "sql_ast/sql_ast.h"

namespace rel2sql {
namespace sql::ast {

/** Dense-renumber generated aliases (T|E|I|R + digits) within one top-level SQL statement. */
class TableAliasRenumberer {
 public:
  static void Renumber(Expression& root);

 private:
  static std::unordered_map<std::string, std::string> CollectRenameMap(Expression& root);
  static void ApplyRenameMap(Expression& root, const std::unordered_map<std::string, std::string>& rename_map);
};

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // TABLE_ALIAS_RENUMBERER_H
