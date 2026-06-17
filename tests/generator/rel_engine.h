#ifndef REL2SQL_TESTS_GENERATOR_REL_ENGINE_H_
#define REL2SQL_TESTS_GENERATOR_REL_ENGINE_H_

#include <string>

#include "generator/data_fixture.h"
#include "generator/result_set.h"

namespace rel2sql::generator {

class RelEngine {
 public:
  static bool IsAvailable();

  // Runs a Rel program on the fixture and returns the output relation result.
  static ResultSet Run(const std::string& rel_program, const DataFixture& fixture, const std::string& output_def);
};

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_REL_ENGINE_H_
