#ifndef REL2SQL_H
#define REL2SQL_H

#include "visitors/parse.h"

namespace rel2sql {

/*
 * Translates the given input string into SQL.
 *
 * @param input The input string to be translated.
 * @return The translated SQL string.
 */
inline std::string Translate(std::string_view input) {
  std::ostringstream oss;

  oss << *rel_parser::GetSQL(input);

  return oss.str();
}

}  // namespace rel2sql

#endif  // REL2SQL_H
