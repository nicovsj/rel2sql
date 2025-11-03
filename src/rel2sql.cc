#include <rel2sql/rel2sql.h>

#include "structs/edb_info.h"
#include "translate.h"

namespace rel2sql {

std::string Translate(std::string_view input) { return GetSQL(input)->ToString(); }

std::string Translate(std::string_view input, const EDBMap& edb_map) { return GetSQL(input, edb_map)->ToString(); }

std::string DumbTranslate(std::string_view input) { return GetUnoptimizedSQL(input)->ToString(); }

std::string DumbTranslate(std::string_view input, const EDBMap& edb_map) {
  return GetUnoptimizedSQL(input, edb_map)->ToString();
}

}  // namespace rel2sql
