#ifndef REL2SQL_TESTS_GENERATOR_DUCKDB_SESSION_H_
#define REL2SQL_TESTS_GENERATOR_DUCKDB_SESSION_H_

#include <string>

#include "duckdb.h"
#include "rel_ast/relation_info.h"

namespace rel2sql::testing {

std::string SqlQuoteIdent(const std::string& id);
std::string CreateTableDdl(const std::string& name, const RelationInfo& info);

void RunQueryOrFail(duckdb_connection con, const std::string& sql, const char* ctx);
void RunSqlOrScript(duckdb_connection con, const std::string& sql);
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
