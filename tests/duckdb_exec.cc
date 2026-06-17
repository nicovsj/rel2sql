#include "duckdb_exec.h"

#include "generator/duckdb_session.h"

namespace rel2sql::testing {

void AssertExecutesInDuckDB(const std::string& sql, const RelationMap& edb) {
  DuckDbConnection db;
  ApplyEdbDdl(db.con, edb);
  RunSqlOrScript(db.con, sql);
}

void AssertScriptExecutesInDuckDB(const std::string& script, const RelationMap& edb) {
  AssertExecutesInDuckDB(script, edb);
}

}  // namespace rel2sql::testing
