#include "duckdb_exec.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "duckdb.h"
#include "gtest/gtest.h"
#include "rel_ast/relation_info.h"

namespace rel2sql::testing {
namespace {

std::string SqlQuoteIdent(const std::string& id) {
  std::string out = "\"";
  for (char c : id) {
    if (c == '"') {
      out += "\"\"";
    } else {
      out += c;
    }
  }
  out += '"';
  return out;
}

std::string SqlTypeForColumn(const std::string& relation_name, int col_index, int arity,
                             const std::unordered_set<std::string>* varchar_value_relations,
                             const std::unordered_set<std::string>* date_value_relations) {
  const bool is_value_col = (arity >= 3 && col_index == arity - 1) || (arity == 2 && col_index == 1);
  if (date_value_relations && date_value_relations->count(relation_name) != 0 && is_value_col) {
    return "DATE";
  }
  if (!varchar_value_relations || varchar_value_relations->count(relation_name) == 0) {
    return "DOUBLE";
  }
  // Ternary lineitem/partsupp attrs: value is the last column (A3).
  if (arity >= 3 && col_index == arity - 1) {
    return "VARCHAR";
  }
  // Binary TPC-H projections: rel2sql often uses A1 for string values (like_match, literals);
  // use VARCHAR on both columns for empty-schema smoke tests (no data rows).
  if (arity == 2) {
    return "VARCHAR";
  }
  return "DOUBLE";
}

std::string CreateTableDdl(const std::string& name, const RelationInfo& info,
                           const std::unordered_set<std::string>* varchar_value_relations,
                           const std::unordered_set<std::string>* date_value_relations) {
  std::ostringstream os;
  os << "CREATE TABLE " << SqlQuoteIdent(name) << " (";
  for (int i = 0; i < info.arity; ++i) {
    if (i > 0) {
      os << ", ";
    }
    os << SqlQuoteIdent(info.AttributeName(i)) << " "
       << SqlTypeForColumn(name, i, info.arity, varchar_value_relations, date_value_relations);
  }
  os << ");";
  return os.str();
}

bool RunQuery(duckdb_connection con, const std::string& sql, const char* ctx, std::string* error_out) {
  duckdb_result result;
  memset(&result, 0, sizeof(result));
  duckdb_state st = duckdb_query(con, sql.c_str(), &result);
  const char* err = duckdb_result_error(&result);
  std::string err_msg = err ? err : "";
  duckdb_destroy_result(&result);
  if (st != DuckDBSuccess) {
    if (error_out) {
      *error_out = std::string("DuckDB ") + ctx + ": " + err_msg + "\nSQL:\n" + sql;
    }
    return false;
  }
  return true;
}

void RunQueryOrFail(duckdb_connection con, const std::string& sql, const char* ctx) {
  std::string err;
  ASSERT_TRUE(RunQuery(con, sql, ctx, &err)) << err;
}

std::string TrimSql(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  while (!s.empty() && s.back() == ';') {
    s.pop_back();
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
      s.pop_back();
    }
  }
  return s;
}

bool RunScriptStatementWise(duckdb_connection con, const std::string& script, std::string* error_out) {
  std::string cur;
  cur.reserve(script.size());
  for (size_t i = 0; i < script.size(); ++i) {
    char c = script[i];
    if (c == ';') {
      std::string stmt = TrimSql(cur);
      if (!stmt.empty()) {
        if (!RunQuery(con, stmt, "statement in script", error_out)) return false;
      }
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  std::string tail = TrimSql(cur);
  if (!tail.empty()) {
    if (!RunQuery(con, tail, "final statement in script", error_out)) return false;
  }
  return true;
}

bool RunSqlOrScript(duckdb_connection con, const std::string& sql, std::string* error_out) {
  duckdb_result result;
  memset(&result, 0, sizeof(result));
  duckdb_state st = duckdb_query(con, sql.c_str(), &result);
  duckdb_destroy_result(&result);
  if (st == DuckDBSuccess) {
    return true;
  }
  return RunScriptStatementWise(con, sql, error_out);
}

bool ApplyEdbDdl(duckdb_connection con, const RelationMap& edb, std::string* error_out,
                 const std::unordered_set<std::string>* varchar_value_relations,
                 const std::unordered_set<std::string>* date_value_relations) {
  std::vector<std::string> names;
  names.reserve(edb.map.size());
  for (const auto& kv : edb.map) {
    names.push_back(kv.first);
  }
  std::sort(names.begin(), names.end());

  for (const auto& name : names) {
    const RelationInfo& info = edb.map.at(name);
    std::string ddl = CreateTableDdl(name, info, varchar_value_relations, date_value_relations);
    if (!RunQuery(con, ddl, "DDL", error_out)) return false;
  }
  return true;
}

DuckDbResultSet FetchResult(duckdb_result* result) {
  DuckDbResultSet out;
  const idx_t ncols = duckdb_column_count(result);
  const idx_t nrows = duckdb_row_count(result);
  out.column_names.reserve(ncols);
  for (idx_t c = 0; c < ncols; ++c) {
    const char* name = duckdb_column_name(result, c);
    out.column_names.push_back(name ? name : "");
  }
  out.rows.reserve(nrows);
  for (idx_t r = 0; r < nrows; ++r) {
    std::vector<std::string> row;
    row.reserve(ncols);
    for (idx_t c = 0; c < ncols; ++c) {
      char* val = duckdb_value_varchar(result, c, r);
      row.push_back(val ? val : "");
      duckdb_free(val);
    }
    out.rows.push_back(std::move(row));
  }
  return out;
}

bool ApproxEqual(const std::string& a, const std::string& b, double tol) {
  if (a == b) return true;
  try {
    double da = std::stod(a);
    double db = std::stod(b);
    if (!std::isfinite(da) || !std::isfinite(db)) return false;
    return std::abs(da - db) <= tol;
  } catch (...) {
    return false;
  }
}

std::string RowKey(const std::vector<std::string>& row, double float_tol) {
  std::ostringstream os;
  for (size_t i = 0; i < row.size(); ++i) {
    if (i > 0) os << '\t';
    os << row[i];
  }
  (void)float_tol;
  return os.str();
}

}  // namespace

DuckDbSession::~DuckDbSession() {
  if (con) {
    duckdb_disconnect(&con);
    con = nullptr;
  }
  if (db) {
    duckdb_close(&db);
    db = nullptr;
  }
}

RelationMap LoadTpchEdbFromFile(const std::string& path) { return LoadTpchEdbForDuckDb(path).relations; }

std::unordered_set<std::string> LoadTpchCompanionRelationSet(const std::filesystem::path& types_path) {
  std::unordered_set<std::string> out;
  std::ifstream in(types_path);
  if (!in) {
    return out;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream iss(line);
    std::string name;
    if (iss >> name) {
      out.insert(name);
    }
  }
  return out;
}

std::unordered_set<std::string> LoadTpchVarcharValueRelations(const std::string& tpch_edb_path) {
  const std::filesystem::path edb_path(tpch_edb_path);
  return LoadTpchCompanionRelationSet(edb_path.parent_path() / "tpch_edb_duckdb_types.edb");
}

std::unordered_set<std::string> LoadTpchDateValueRelations(const std::string& tpch_edb_path) {
  const std::filesystem::path edb_path(tpch_edb_path);
  return LoadTpchCompanionRelationSet(edb_path.parent_path() / "tpch_edb_duckdb_date_types.edb");
}

TpchEdbForDuckDb LoadTpchEdbForDuckDb(const std::string& tpch_edb_path) {
  TpchEdbForDuckDb out;
  out.varchar_value_relations = LoadTpchVarcharValueRelations(tpch_edb_path);
  out.date_value_relations = LoadTpchDateValueRelations(tpch_edb_path);
  std::ifstream in(tpch_edb_path);
  if (!in) {
    return out;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream iss(line);
    std::string name;
    int arity = 0;
    if (!(iss >> name >> arity)) continue;
    out.relations.set(name, RelationInfo(arity));
  }
  return out;
}

std::string OpenInMemorySession(DuckDbSession* session, const RelationMap& edb,
                                const std::unordered_set<std::string>* varchar_value_relations,
                                const std::unordered_set<std::string>* date_value_relations) {
  if (!session) return "null session";
  duckdb_database db = nullptr;
  if (duckdb_open(nullptr, &db) != DuckDBSuccess) {
    return "duckdb_open failed";
  }
  duckdb_connection con = nullptr;
  if (duckdb_connect(db, &con) != DuckDBSuccess) {
    duckdb_close(&db);
    return "duckdb_connect failed";
  }
  std::string err;
  if (!ApplyEdbDdl(con, edb, &err, varchar_value_relations, date_value_relations)) {
    duckdb_disconnect(&con);
    duckdb_close(&db);
    return err;
  }
  session->db = db;
  session->con = con;
  return {};
}

std::string OpenFileSession(DuckDbSession* session, const std::string& db_path) {
  if (!session) return "null session";
  duckdb_database db = nullptr;
  if (duckdb_open(db_path.c_str(), &db) != DuckDBSuccess) {
    return "duckdb_open file failed: " + db_path;
  }
  duckdb_connection con = nullptr;
  if (duckdb_connect(db, &con) != DuckDBSuccess) {
    duckdb_close(&db);
    return "duckdb_connect failed";
  }
  session->db = db;
  session->con = con;
  return {};
}

bool ExecuteScriptOnConnection(duckdb_connection con, const std::string& script, std::string* error_out) {
  return RunSqlOrScript(con, script, error_out);
}

DuckDbResultSet ExecuteQueryOnConnection(duckdb_connection con, const std::string& sql, std::string* error_out) {
  DuckDbResultSet empty;
  duckdb_result result;
  memset(&result, 0, sizeof(result));
  duckdb_state st = duckdb_query(con, sql.c_str(), &result);
  const char* err = duckdb_result_error(&result);
  if (st != DuckDBSuccess) {
    if (error_out) {
      *error_out = std::string(err ? err : "unknown DuckDB error") + "\nSQL:\n" + sql;
    }
    duckdb_destroy_result(&result);
    return empty;
  }
  auto fetched = FetchResult(&result);
  duckdb_destroy_result(&result);
  return fetched;
}

bool ResultSetsEqual(const DuckDbResultSet& lhs, const DuckDbResultSet& rhs, double float_abs_tol,
                     bool order_insensitive, std::string* diff_message) {
  if (lhs.column_names.size() != rhs.column_names.size()) {
    if (diff_message) *diff_message = "column count mismatch";
    return false;
  }
  for (size_t i = 0; i < lhs.column_names.size(); ++i) {
    if (lhs.column_names[i] != rhs.column_names[i]) {
      if (diff_message) *diff_message = "column name mismatch at " + std::to_string(i);
      return false;
    }
  }

  if (!order_insensitive) {
    if (lhs.rows.size() != rhs.rows.size()) {
      if (diff_message) *diff_message = "row count mismatch";
      return false;
    }
    for (size_t r = 0; r < lhs.rows.size(); ++r) {
      if (lhs.rows[r].size() != rhs.rows[r].size()) {
        if (diff_message) *diff_message = "width mismatch at row " + std::to_string(r);
        return false;
      }
      for (size_t c = 0; c < lhs.rows[r].size(); ++c) {
        const std::string& a = lhs.rows[r][c];
        const std::string& b = rhs.rows[r][c];
        if (!(a == b || ApproxEqual(a, b, float_abs_tol))) {
          if (diff_message) {
            *diff_message = "cell mismatch at row " + std::to_string(r) + " col " + std::to_string(c);
          }
          return false;
        }
      }
    }
    return true;
  }

  std::vector<std::vector<std::string>> rem_r = rhs.rows;
  for (const auto& lr : lhs.rows) {
    bool found = false;
    for (size_t j = 0; j < rem_r.size(); ++j) {
      if (rem_r[j].size() != lr.size()) continue;
      bool match = true;
      for (size_t c = 0; c < lr.size(); ++c) {
        if (!(lr[c] == rem_r[j][c] || ApproxEqual(lr[c], rem_r[j][c], float_abs_tol))) {
          match = false;
          break;
        }
      }
      if (match) {
        rem_r.erase(rem_r.begin() + static_cast<std::ptrdiff_t>(j));
        found = true;
        break;
      }
    }
    if (!found) {
      if (diff_message) *diff_message = "lhs row not found in rhs";
      return false;
    }
  }
  if (!rem_r.empty()) {
    if (diff_message) *diff_message = "extra rows in rhs";
    return false;
  }
  return true;
}

void AssertResultSetsEqual(const DuckDbResultSet& lhs, const DuckDbResultSet& rhs, double float_abs_tol,
                           bool order_insensitive) {
  std::string diff;
  ASSERT_TRUE(ResultSetsEqual(lhs, rhs, float_abs_tol, order_insensitive, &diff)) << diff;
}

void AssertExecutesInDuckDB(const std::string& sql, const RelationMap& edb) {
  DuckDbSession session;
  std::string err = OpenInMemorySession(&session, edb, nullptr);
  ASSERT_TRUE(err.empty()) << err;
  err.clear();
  ASSERT_TRUE(ExecuteScriptOnConnection(session.con, sql, &err)) << err;
}

void AssertScriptExecutesInDuckDB(const std::string& script, const RelationMap& edb) {
  AssertExecutesInDuckDB(script, edb);
}

}  // namespace rel2sql::testing
