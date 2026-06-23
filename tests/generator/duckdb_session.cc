#include "generator/duckdb_session.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace rel2sql::testing {
namespace {

std::string TrimSql(std::string s) {
  while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) != 0)) {
    s.erase(s.begin());
  }
  while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) != 0)) {
    s.pop_back();
  }
  while (!s.empty() && s.back() == ';') {
    s.pop_back();
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) != 0)) {
      s.pop_back();
    }
  }
  return s;
}

void RunScriptStatementWise(duckdb_connection con, const std::string& script) {
  std::string cur;
  cur.reserve(script.size());
  for (size_t i = 0; i < script.size(); ++i) {
    char c = script[i];
    if (c == ';') {
      std::string stmt = TrimSql(cur);
      if (!stmt.empty()) {
        RunQueryOrFail(con, stmt, "statement in script");
      }
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  std::string tail = TrimSql(cur);
  if (!tail.empty()) {
    RunQueryOrFail(con, tail, "final statement in script");
  }
}

}  // namespace

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

std::string CreateTableDdl(const std::string& name, const RelationInfo& info) {
  std::ostringstream os;
  os << "CREATE TABLE " << SqlQuoteIdent(name) << " (";
  for (int i = 0; i < info.arity; ++i) {
    if (i > 0) {
      os << ", ";
    }
    os << SqlQuoteIdent(info.AttributeName(i)) << " DOUBLE";
  }
  os << ");";
  return os.str();
}

void RunQueryOrFail(duckdb_connection con, const std::string& sql, const char* ctx) {
  duckdb_result result;
  memset(&result, 0, sizeof(result));
  duckdb_state st = duckdb_query(con, sql.c_str(), &result);
  const char* err = duckdb_result_error(&result);
  std::string err_msg = (err != nullptr) ? err : "";
  duckdb_destroy_result(&result);
  ASSERT_EQ(st, DuckDBSuccess) << "DuckDB " << ctx << ": " << err_msg << "\nSQL:\n" << sql;
}

void RunSqlOrScript(duckdb_connection con, const std::string& sql) {
  duckdb_result result;
  memset(&result, 0, sizeof(result));
  duckdb_state st = duckdb_query(con, sql.c_str(), &result);
  duckdb_destroy_result(&result);
  if (st == DuckDBSuccess) {
    return;
  }
  RunScriptStatementWise(con, sql);
}

std::vector<std::string> SplitSqlStatements(const std::string& script) {
  std::vector<std::string> statements;
  std::string cur;
  cur.reserve(script.size());
  for (char c : script) {
    if (c == ';') {
      std::string stmt = TrimSql(cur);
      if (!stmt.empty()) {
        statements.push_back(std::move(stmt));
      }
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  std::string tail = TrimSql(cur);
  if (!tail.empty()) {
    statements.push_back(std::move(tail));
  }
  return statements;
}

bool LooksLikeSelectStatement(const std::string& sql) {
  const std::string trimmed = TrimSql(sql);
  if (trimmed.size() < 6) return false;
  for (size_t i = 0; i < 6; ++i) {
    if (std::toupper(static_cast<unsigned char>(trimmed[i])) != "SELECT"[i]) {
      return false;
    }
  }
  return true;
}

void ApplyEdbDdl(duckdb_connection con, const RelationMap& edb) {
  std::vector<std::string> names;
  names.reserve(edb.map.size());
  for (const auto& kv : edb.map) {
    names.push_back(kv.first);
  }
  std::sort(names.begin(), names.end());

  for (const auto& name : names) {
    const RelationInfo& info = edb.map.at(name);
    std::string ddl = CreateTableDdl(name, info);
    RunQueryOrFail(con, ddl, "DDL");
  }
}

DuckDbConnection::DuckDbConnection() {
  if (duckdb_open(nullptr, &db) != DuckDBSuccess) {
    throw std::runtime_error("duckdb_open failed");
  }
  if (duckdb_connect(db, &con) != DuckDBSuccess) {
    duckdb_close(&db);
    db = nullptr;
    throw std::runtime_error("duckdb_connect failed");
  }
}

DuckDbConnection::~DuckDbConnection() {
  if (con != nullptr) duckdb_disconnect(&con);
  if (db != nullptr) duckdb_close(&db);
}

}  // namespace rel2sql::testing
