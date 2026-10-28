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
  // Selects another program for the same slot (the corpus builder retries a slot until Rel gives a non-empty
  // answer). Variant 0 is the program generated before variants existed.
  uint64_t variant = 0;
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
// Retries with derived RNG streams on pipeline verification failure. With require_translation false, only the
// well-formedness checks apply, so the caller can count programs the translation rejects.
GeneratedProgram GenerateProgram(const SuiteConfig& config, bool require_translation = true);

// Returns true when the program passes the generator's checks that do not involve the translation: output def,
// connected def graph, forall domains, full-application arities, and no statically false comparison.
bool IsWellFormedProgram(const std::string& source, const RelationMap& edb_map);

// Returns true when the program is well-formed and translates without throwing.
bool VerifyProgram(const std::string& source, const RelationMap& edb_map);

// Returns false when an exists/forall binding variable does not appear in its body.
bool QuantifierBindingsAreUsed(const std::string& source);

// Returns false when a forall quantifier omits required (var in EDB) domains.
bool ForallBindingsHaveDomains(const std::string& source);

// Returns true when the program contains a comparison that is always false (e.g. z - z = 1).
bool ProgramContainsStaticallyFalseComparison(const std::string& source);

// Returns true when the last program def transitively depends on every earlier Gen* def.
bool ProgramHasConnectedDependencyGraph(const std::string& source);

// Returns true when integer and float values may meet in the program: a def computing an average is used by
// another def. Rel distinguishes the integer 3 from the float 3.0, while SQL and the semantics of the paper's
// fragment have a single numeric domain, so such programs are outside the fragment.
bool ProgramMixesIntegersAndFloats(const std::string& source);

// Name of the last `def` in program source (the oracle output relation).
std::optional<std::string> ProgramOutputDefName(const std::string& source);

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_REL_PROGRAM_GENERATOR_H_
