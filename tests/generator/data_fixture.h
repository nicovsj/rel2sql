#ifndef REL2SQL_TESTS_GENERATOR_DATA_FIXTURE_H_
#define REL2SQL_TESTS_GENERATOR_DATA_FIXTURE_H_

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "duckdb.h"
#include "generator/rel_program_generator.h"
#include "rel_ast/relation_info.h"

namespace rel2sql::generator {

using TableRows = std::vector<std::vector<double>>;

class DataFixture {
 public:
  static DataFixture Create(const SuiteConfig& config, const RelationMap& schema, size_t rows_per_table = 10);

  // Same EDB rows for every program (seed/index fixed). Used by the golden corpus oracle.
  static DataFixture CreateCanonical(const RelationMap& edb_map, size_t rows_per_table = 10);

  static DataFixture FromRows(const RelationMap& schema, const std::unordered_map<std::string, TableRows>& rows);

  const RelationMap& Schema() const { return schema_; }
  const std::unordered_map<std::string, TableRows>& Rows() const { return rows_; }

  void LoadInto(duckdb_connection con) const;

 private:
  RelationMap schema_;
  std::unordered_map<std::string, TableRows> rows_;
};

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_DATA_FIXTURE_H_
