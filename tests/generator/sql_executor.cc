#include "generator/sql_executor.h"

#include <cstring>
#include <sstream>
#include <stdexcept>

#include "duckdb.h"
#include "generator/duckdb_session.h"

namespace rel2sql::generator {
namespace {

void CheckSuccess(duckdb_state st, const char* err, const std::string& query_sql) {
  if (st != DuckDBSuccess) {
    std::ostringstream os;
    os << "DuckDB query failed: " << (err ? err : "") << "\nSQL:\n" << query_sql;
    throw std::runtime_error(os.str());
  }
}

ResultSet ReadResult(duckdb_connection con, const std::string& query_sql) {
  duckdb_result result;
  memset(&result, 0, sizeof(result));
  const duckdb_state st = duckdb_query(con, query_sql.c_str(), &result);
  const char* err = duckdb_result_error(&result);
  CheckSuccess(st, err, query_sql);

  ResultSet out;
  const idx_t col_count = duckdb_column_count(&result);
  for (idx_t c = 0; c < col_count; ++c) {
    const char* name = duckdb_column_name(&result, c);
    out.column_names.push_back(name ? name : "");
  }

  const idx_t row_count = duckdb_row_count(&result);
  for (idx_t r = 0; r < row_count; ++r) {
    Row row;
    row.values.reserve(static_cast<size_t>(col_count));
    for (idx_t c = 0; c < col_count; ++c) {
      if (duckdb_value_is_null(&result, c, r)) {
        row.values.push_back("NULL");
      } else {
        char* value = duckdb_value_varchar(&result, c, r);
        row.values.push_back(value ? value : "");
        if (value) duckdb_free(value);
      }
    }
    out.rows.push_back(std::move(row));
  }

  duckdb_destroy_result(&result);
  return Canonicalize(std::move(out));
}

}  // namespace

ResultSet SqlExecutor::RunQuery(const std::string& sql, const DataFixture& fixture) {
  testing::DuckDbConnection db;
  fixture.LoadInto(db.con);
  return ReadResult(db.con, sql);
}

ResultSet SqlExecutor::RunProgram(const std::string& sql_script, const DataFixture& fixture,
                                  const std::string& output_def) {
  testing::DuckDbConnection db;
  fixture.LoadInto(db.con);
  testing::RunSqlOrScript(db.con, sql_script);

  const int arity = fixture.Schema().map.at(output_def).arity;
  std::ostringstream query;
  query << "SELECT * FROM " << testing::SqlQuoteIdent(output_def) << " ORDER BY 1";
  for (int i = 1; i < arity; ++i) {
    query << ", " << (i + 1);
  }
  return ReadResult(db.con, query.str());
}

}  // namespace rel2sql::generator
