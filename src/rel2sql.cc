#include <rel2sql/rel2sql.h>

#include <unordered_map>

#include "structs/edb_info.h"
#include "translate.h"

namespace rel2sql {

std::string Translate(std::string_view input) {
  std::ostringstream oss;
  oss << *GetSQL(input);
  return oss.str();
}

std::string Translate(std::string_view input, const std::unordered_map<std::string, int>& external_arity_map) {
  // Convert legacy format to EDBMap and delegate to the main implementation
  return Translate(input, edb_utils::FromArityMap(external_arity_map));
}

std::string DumbTranslate(std::string_view input) {
  std::ostringstream oss;
  oss << *GetUnoptimizedSQL(input);
  return oss.str();
}

std::string DumbTranslate(std::string_view input, const std::unordered_map<std::string, int>& external_arity_map) {
  // Convert legacy format to EDBMap and delegate to the main implementation
  return DumbTranslate(input, edb_utils::FromArityMap(external_arity_map));
}

std::string Translate(std::string_view input, const EDBMap& edb_map) {
  std::ostringstream oss;
  oss << *GetSQL(input, edb_map);
  return oss.str();
}

std::string DumbTranslate(std::string_view input, const EDBMap& edb_map) {
  std::ostringstream oss;
  oss << *GetUnoptimizedSQL(input, edb_map);
  return oss.str();
}

}  // namespace rel2sql
