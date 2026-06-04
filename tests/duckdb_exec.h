#ifndef REL2SQL_TESTS_DUCKDB_EXEC_H_
#define REL2SQL_TESTS_DUCKDB_EXEC_H_

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "duckdb.h"
#include "rel_ast/relation_info.h"

namespace rel2sql {
namespace testing {

struct DuckDbResultSet {
  std::vector<std::string> column_names;
  std::vector<std::vector<std::string>> rows;
};

// Parse `tpch_edb.edb` lines: `relation_name arity` (comments and blanks skipped).
RelationMap LoadTpchEdbFromFile(const std::string& path);

// Relations whose last attribute (value column) is VARCHAR in DuckDB empty-schema DDL.
// Reads `tpch_edb_duckdb_types.edb` beside `tpch_edb.edb` when present.
std::unordered_set<std::string> LoadTpchVarcharValueRelations(const std::string& tpch_edb_path);

struct TpchEdbForDuckDb {
  RelationMap relations;
  std::unordered_set<std::string> varchar_value_relations;
};

// Loads arity map + optional companion DuckDB type hints for TPC-H benchmarks.
TpchEdbForDuckDb LoadTpchEdbForDuckDb(const std::string& tpch_edb_path);

struct DuckDbSession {
  duckdb_database db = nullptr;
  duckdb_connection con = nullptr;
  ~DuckDbSession();
};

// In-memory DB with empty EDB tables. Returns error message on failure.
std::string OpenInMemorySession(DuckDbSession* session, const RelationMap& edb,
                                const std::unordered_set<std::string>* varchar_value_relations = nullptr);

// Open existing DuckDB file (local compare). Returns error message on failure.
std::string OpenFileSession(DuckDbSession* session, const std::string& db_path);

bool ExecuteScriptOnConnection(duckdb_connection con, const std::string& script, std::string* error_out);

DuckDbResultSet ExecuteQueryOnConnection(duckdb_connection con, const std::string& sql, std::string* error_out);

// Multiset compare when order_insensitive; exact row order when false.
void AssertResultSetsEqual(const DuckDbResultSet& lhs, const DuckDbResultSet& rhs, double float_abs_tol = 1.0,
                           bool order_insensitive = true);

// Non-gtest variant for CLI tools. Sets diff_message on failure.
bool ResultSetsEqual(const DuckDbResultSet& lhs, const DuckDbResultSet& rhs, double float_abs_tol,
                     bool order_insensitive, std::string* diff_message);

// Creates empty base tables for every EDB in `edb`, then runs `sql` on an in-memory DuckDB.
// Fails the test (GoogleTest) if DDL or the query errors.
void AssertExecutesInDuckDB(const std::string& sql, const RelationMap& edb);

// Same as AssertExecutesInDuckDB but for scripts with multiple statements (e.g. several CREATE VIEW).
void AssertScriptExecutesInDuckDB(const std::string& script, const RelationMap& edb);

}  // namespace testing
}  // namespace rel2sql

#endif  // REL2SQL_TESTS_DUCKDB_EXEC_H_
