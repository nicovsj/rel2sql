#ifndef REL2SQL_TESTS_GENERATOR_DUCKDB_SESSION_H_
#define REL2SQL_TESTS_GENERATOR_DUCKDB_SESSION_H_

#include <string>
#include <vector>

#include "duckdb.h"
#include "duckdb_exec.h"
#include "rel_ast/relation_info.h"

namespace rel2sql::testing {

// SqlQuoteIdent and RunQueryOrFail come from duckdb_exec.h.

void RunSqlOrScript(duckdb_connection con, const std::string& sql);
std::vector<std::string> SplitSqlStatements(const std::string& script);
bool LooksLikeSelectStatement(const std::string& sql);
void ApplyEdbDdl(duckdb_connection con, const RelationMap& edb);

struct DuckDbConnection {
  duckdb_database db = nullptr;
  duckdb_connection con = nullptr;

  DuckDbConnection();
  ~DuckDbConnection();

  DuckDbConnection(const DuckDbConnection&) = delete;
  DuckDbConnection& operator=(const DuckDbConnection&) = delete;
};

}  // namespace rel2sql::testing

#endif  // REL2SQL_TESTS_GENERATOR_DUCKDB_SESSION_H_
