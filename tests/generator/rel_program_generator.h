#ifndef REL2SQL_TESTS_GENERATOR_REL_PROGRAM_GENERATOR_H_
#define REL2SQL_TESTS_GENERATOR_REL_PROGRAM_GENERATOR_H_

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "rel_ast/relation_info.h"

namespace rel2sql::generator {

struct GeneratorProfile {
  bool allow_recursion = false;
  bool allow_forall = false;
  bool allow_aggregates = false;
  bool allow_extensional = false;
  bool allow_partial_app = false;
  bool allow_products = false;
  int max_defs = 4;
};

struct SuiteConfig {
  uint64_t seed = 0;
  size_t program_index = 0;
  size_t node_budget = 6;
  RelationMap edb_map;
  GeneratorProfile profile;
};

struct GeneratedProgram {
  std::string source;
  RelationMap schema;
  std::string output_def;
};

class GenerationException : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Generates a valid Rel program deterministically from config.
// Retries with derived RNG streams on pipeline verification failure.
GeneratedProgram GenerateProgram(const SuiteConfig& config);

// Returns true when the program translates without throwing.
bool VerifyProgram(const std::string& source, const RelationMap& edb_map);

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_REL_PROGRAM_GENERATOR_H_
