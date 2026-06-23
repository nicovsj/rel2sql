#ifndef REL2SQL_TESTS_GENERATOR_REL_ENGINE_H_
#define REL2SQL_TESTS_GENERATOR_REL_ENGINE_H_

#include <string>

#include "generator/data_fixture.h"
#include "generator/result_set.h"
#include "rel_ast/relation_info.h"

namespace rel2sql::generator {

class RelEngine {
 public:
  static bool IsAvailable();

  // True when the persistent Unix-socket server responds to ping.
  static bool IsServerReachable();

  // `output_schema` must contain `output_def` (typically `program.schema`). When null, uses
  // `fixture.Schema()` (only valid when the fixture was built with the full program schema).
  static ResultSet Run(const std::string& rel_program, const DataFixture& fixture, const std::string& output_def,
                       const RelationMap* output_schema = nullptr);
};

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_REL_ENGINE_H_
