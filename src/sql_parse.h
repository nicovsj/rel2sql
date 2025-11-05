#ifndef SQL_PARSE_H
#define SQL_PARSE_H

#include <string>

#include "structs/edb_info.h"
#include "structs/sql_ast.h"

namespace rel2sql {

/*
 * Parses SQL text into the SQL AST structure.
 *
 * @param sql_text The SQL text to parse.
 * @return A shared pointer to the parsed SQL AST Expression.
 * @throws ParseException if the SQL syntax is invalid.
 */
std::shared_ptr<sql::ast::Expression> ParseSQL(const std::string& sql_text, const EDBMap& edb_map = EDBMap());

}  // namespace rel2sql

#endif  // SQL_PARSE_H
