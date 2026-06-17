#include "generator/data_fixture.h"

#include <algorithm>
#include <sstream>

#include "duckdb.h"
#include "generator/duckdb_session.h"
#include "generator/rng.h"
#include "gtest/gtest.h"

namespace rel2sql::generator {
namespace {

constexpr uint64_t kDataFixtureSalt = 0xD4FA7A5EULL;

void InsertRows(duckdb_connection con, const std::string& table_name, const RelationInfo& info, const TableRows& rows) {
  for (const auto& row : rows) {
    ASSERT_EQ(row.size(), static_cast<size_t>(info.arity));
    std::ostringstream os;
    os << "INSERT INTO " << testing::SqlQuoteIdent(table_name) << " VALUES (";
    for (size_t i = 0; i < row.size(); ++i) {
      if (i > 0) os << ", ";
      os << row[i];
    }
    os << ");";
    testing::RunQueryOrFail(con, os.str(), "INSERT");
  }
}

}  // namespace

DataFixture DataFixture::Create(const SuiteConfig& config, const RelationMap& schema, size_t rows_per_table) {
  Rng rng(MixSeed(config.seed, config.program_index, kDataFixtureSalt));

  DataFixture fixture;
  fixture.schema_ = schema;

  std::vector<std::string> names;
  names.reserve(schema.map.size());
  for (const auto& kv : schema.map) {
    names.push_back(kv.first);
  }
  std::sort(names.begin(), names.end());

  for (const auto& name : names) {
    const RelationInfo& info = schema.map.at(name);
    if (!config.edb_map.has(name)) {
      continue;
    }

    TableRows table_rows;
    table_rows.reserve(rows_per_table);
    for (size_t r = 0; r < rows_per_table; ++r) {
      std::vector<double> row;
      row.reserve(static_cast<size_t>(info.arity));
      for (int c = 0; c < info.arity; ++c) {
        row.push_back(static_cast<double>(rng.UniformInt(0, 20)));
      }
      table_rows.push_back(std::move(row));
    }
    fixture.rows_[name] = std::move(table_rows);
  }

  return fixture;
}

void DataFixture::LoadInto(duckdb_connection con) const {
  RelationMap edb_only;
  for (const auto& kv : rows_) {
    edb_only[kv.first] = schema_.map.at(kv.first);
  }
  testing::ApplyEdbDdl(con, edb_only);
  for (const auto& kv : rows_) {
    const RelationInfo& info = schema_.map.at(kv.first);
    InsertRows(con, kv.first, info, kv.second);
  }
}

}  // namespace rel2sql::generator
