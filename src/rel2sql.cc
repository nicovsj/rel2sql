#include "rel2sql.h"

#include "translate.h"

namespace rel2sql {

std::string Translate(std::string_view input) {
  std::ostringstream oss;
  oss << *rel_parser::GetSQL(input);
  return oss.str();
}

std::string DumbTranslate(std::string_view input) {
  std::ostringstream oss;
  oss << *rel_parser::GetUnoptimizedSQL(input);
  return oss.str();
}

}  // namespace rel2sql
