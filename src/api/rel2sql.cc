#include <rel2sql/rel2sql.h>

#include "api/translate.h"
#include "rel_ast/relation_info.h"

namespace rel2sql {

std::string Translate(std::string_view input) { return GetSQLRel(input)->ToString(); }

std::string Translate(std::string_view input, const RelationMap& edb_map) {
  return GetSQLRel(input, edb_map)->ToString();
}

std::string DumbTranslate(std::string_view input) { return GetUnoptimizedSQLRel(input)->ToString(); }

std::string DumbTranslate(std::string_view input, const RelationMap& edb_map) {
  return GetUnoptimizedSQLRel(input, edb_map)->ToString();
}

}  // namespace rel2sql
