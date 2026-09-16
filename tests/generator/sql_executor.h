#ifndef REL2SQL_TESTS_GENERATOR_SQL_EXECUTOR_H_
#define REL2SQL_TESTS_GENERATOR_SQL_EXECUTOR_H_

#include <string>

#include "generator/data_fixture.h"
#include "generator/result_set.h"
#include "rel_ast/relation_info.h"

namespace rel2sql::generator {

class SqlExecutor {
 public:
  // Runs a translated SQL script on the fixture, then queries `output_def`.
  static ResultSet RunProgram(const std::string& sql_script, const DataFixture& fixture, const std::string& output_def,
                              const RelationMap* output_schema = nullptr);

  static ResultSet RunQuery(const std::string& sql, const DataFixture& fixture);
};

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_SQL_EXECUTOR_H_
