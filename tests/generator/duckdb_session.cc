#include "generator/duckdb_session.h"

#include <cctype>
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

}  // namespace

void RunSqlOrScript(duckdb_connection con, const std::string& sql) {
  std::string err;
  ASSERT_TRUE(ExecuteScriptOnConnection(con, sql, &err)) << err;
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
  std::string err;
  ASSERT_TRUE(ApplyEdbDdl(con, edb, &err)) << err;
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
