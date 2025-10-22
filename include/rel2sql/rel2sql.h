#ifndef REL2SQL_REL2SQL_H
#define REL2SQL_REL2SQL_H

#include <string>
#include <string_view>
#include <unordered_map>

namespace rel2sql {

// Forward declarations
class EDBInfo;
class EDBMap;
/*
 * Translates the given input string into SQL.
 *
 * @param input The input string to be translated.
 * @return The translated SQL string.
 */
std::string Translate(std::string_view input);

/*
 * Translates the given input string into SQL with external database information.
 *
 * @param input The input string to be translated.
 * @param external_arity_map Map of relation names to their arities for external databases (legacy format).
 * @return The translated SQL string.
 */
std::string Translate(std::string_view input, const std::unordered_map<std::string, int>& external_arity_map);

/*
 * Translates the given input string into SQL with external database information.
 *
 * @param input The input string to be translated.
 * @param edb_map Map of relation names to their EDB information (arity and optional attribute names).
 * @return The translated SQL string.
 */
std::string Translate(std::string_view input, const EDBMap& edb_map);

/*
 * Translates the given input string into SQL without optimizations.
 *
 * @param input The input string to be translated.
 * @return The translated SQL string.
 */
std::string DumbTranslate(std::string_view input);

/*
 * Translates the given input string into SQL without optimizations with external database information.
 *
 * @param input The input string to be translated.
 * @param external_arity_map Map of relation names to their arities for external databases (legacy format).
 * @return The translated SQL string.
 */
std::string DumbTranslate(std::string_view input, const std::unordered_map<std::string, int>& external_arity_map);

/*
 * Translates the given input string into SQL without optimizations with external database information.
 *
 * @param input The input string to be translated.
 * @param edb_map Map of relation names to their EDB information (arity and optional attribute names).
 * @return The translated SQL string.
 */
std::string DumbTranslate(std::string_view input, const EDBMap& edb_map);

}  // namespace rel2sql

#endif  // REL2SQL_REL2SQL_H
