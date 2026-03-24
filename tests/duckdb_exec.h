#ifndef REL2SQL_TESTS_DUCKDB_EXEC_H_
#define REL2SQL_TESTS_DUCKDB_EXEC_H_

#include <string>

namespace rel2sql {

struct RelationMap;

namespace testing {

// Creates empty base tables for every EDB in `edb`, then runs `sql` on an in-memory DuckDB.
// Fails the test (GoogleTest) if DDL or the query errors.
void AssertExecutesInDuckDB(const std::string& sql, const RelationMap& edb);

// Same as AssertExecutesInDuckDB but for scripts with multiple statements (e.g. several CREATE VIEW).
void AssertScriptExecutesInDuckDB(const std::string& script, const RelationMap& edb);

}  // namespace testing
}  // namespace rel2sql

#endif  // REL2SQL_TESTS_DUCKDB_EXEC_H_
