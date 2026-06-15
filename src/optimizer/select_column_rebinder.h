#ifndef SELECT_COLUMN_REBINDER_H
#define SELECT_COLUMN_REBINDER_H

#include "sql_ast/sql_ast.h"

namespace rel2sql {
namespace sql::ast {

// After flattening/renumbering, SELECT list columns may still reference a subquery alias
// that was inlined out of FROM. Rebind them to a remaining FROM source that exports the name.
void RebindDanglingSelectColumns(Expression& root);

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // SELECT_COLUMN_REBINDER_H
