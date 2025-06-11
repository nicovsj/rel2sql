#ifndef REL2SQL_REL2SQL_H
#define REL2SQL_REL2SQL_H

#include <string>
#include <string_view>

namespace rel2sql {

/*
 * Translates the given input string into SQL.
 *
 * @param input The input string to be translated.
 * @return The translated SQL string.
 */
std::string Translate(std::string_view input);

/*
 * Translates the given input string into SQL without optimizations.
 *
 * @param input The input string to be translated.
 * @return The translated SQL string.
 */
std::string DumbTranslate(std::string_view input);

}  // namespace rel2sql

#endif  // REL2SQL_REL2SQL_H
