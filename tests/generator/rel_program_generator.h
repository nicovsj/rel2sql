#ifndef REL2SQL_TESTS_GENERATOR_REL_PROGRAM_GENERATOR_H_
#define REL2SQL_TESTS_GENERATOR_REL_PROGRAM_GENERATOR_H_

#include <cstddef>
#include <cstdint>
#include <optional>
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
  bool allow_arithmetic = false;
  bool allow_comparisons = false;
  bool allow_negation = false;
  bool allow_where = false;
  bool allow_binding_exprs = false;
  int max_defs = 1;
  // When set, biases def-body selection toward a feature family (corpus presets).
  bool focus_quantifiers = false;
  bool focus_expr_algebra = false;
  bool focus_aggregates = false;
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

// Returns false when an exists/forall binding variable does not appear in its body.
bool QuantifierBindingsAreUsed(const std::string& source);

// Returns false when a forall quantifier omits required (var in EDB) domains.
bool ForallBindingsHaveDomains(const std::string& source);

// Returns true when the program contains a comparison that is always false (e.g. z - z = 1).
bool ProgramContainsStaticallyFalseComparison(const std::string& source);

// Returns true when the last program def transitively depends on every earlier Gen* def.
bool ProgramHasConnectedDependencyGraph(const std::string& source);

// Name of the last `def` in program source (the oracle output relation).
std::optional<std::string> ProgramOutputDefName(const std::string& source);

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_REL_PROGRAM_GENERATOR_H_
