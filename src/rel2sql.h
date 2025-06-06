#ifndef REL2SQL_H
#define REL2SQL_H

#include <sstream>
#include <string>

#include "rel2sql/translate.h"

namespace rel2sql {

/*
 * Translates the given input string into SQL.
 *
 * @param input The input string to be translated.
 * @return The translated SQL string.
 */
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

#endif  // REL2SQL_H
