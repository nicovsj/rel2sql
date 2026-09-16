// One-off debug binary: print a generated Rel program, translated SQL, fixture, and both result sets.
// Usage: bazel run //tests:print_generated_case -- [seed] [program_index] [node_budget] [--program-only]

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "api/translate.h"
#include "generator/corpus_case_config.h"
#include "generator/data_fixture.h"
#include "generator/rel_engine.h"
#include "generator/rel_program_generator.h"
#include "generator/result_set.h"
#include "generator/sql_executor.h"
#include "test_common.h"

namespace rel2sql::generator {
namespace {

SuiteConfig MakeConfig(uint64_t seed, size_t program_index, size_t node_budget) {
  SuiteConfig config;
  config.seed = seed;
  config.program_index = program_index;
  config.node_budget = node_budget;
  config.edb_map = CreateDefaultEDBMap();
  config.profile = FullCorpusProfile();
  return config;
}

bool HasFlag(int argc, char** argv, const char* flag) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == std::string(flag)) return true;
  }
  return false;
}

std::string FormatResultSet(const ResultSet& rs) {
  std::ostringstream os;
  os << "columns: ";
  for (size_t i = 0; i < rs.column_names.size(); ++i) {
    if (i > 0) os << ", ";
    os << rs.column_names[i];
  }
  os << "\nrows (" << rs.rows.size() << "):\n";
  for (const auto& row : rs.rows) {
    os << "  ";
    for (size_t i = 0; i < row.values.size(); ++i) {
      if (i > 0) os << " | ";
      os << row.values[i];
    }
    os << "\n";
  }
  return os.str();
}

std::string FormatFixture(const DataFixture& fixture) {
  std::ostringstream os;
  for (const auto& kv : fixture.Rows()) {
    os << "  " << kv.first << ":\n";
    for (const auto& row : kv.second) {
      os << "    ";
      for (size_t i = 0; i < row.size(); ++i) {
        if (i > 0) os << ", ";
        os << row[i];
      }
      os << "\n";
    }
  }
  return os.str();
}

}  // namespace
}  // namespace rel2sql::generator

int main(int argc, char** argv) {
  using namespace rel2sql::generator;

  const bool program_only = HasFlag(argc, argv, "--program-only");

  uint64_t seed = 1;
  size_t program_index = 0;
  size_t node_budget = 6;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--program-only") continue;
    positional.push_back(arg);
  }
  if (positional.size() > 0) seed = std::stoull(positional[0]);
  if (positional.size() > 1) program_index = static_cast<size_t>(std::stoull(positional[1]));
  if (positional.size() > 2) node_budget = static_cast<size_t>(std::stoull(positional[2]));

  const auto config = MakeConfig(seed, program_index, node_budget);
  const auto program = GenerateProgram(config);
  const auto fixture = DataFixture::CreateCanonical(config.edb_map);

  auto sql = GetSQLRel(program.source, config.edb_map);
  if (!sql) {
    std::cerr << "translation failed\n";
    return 1;
  }

  std::cout << "=== config ===\n";
  std::cout << "seed=" << seed << " program_index=" << program_index << " node_budget=" << node_budget << "\n";
  std::cout << "output_def=" << program.output_def << "\n\n";

  std::cout << "=== EDB fixture (10 rows per table) ===\n";
  std::cout << FormatFixture(fixture) << "\n";

  std::cout << "=== generated Rel program ===\n";
  std::cout << program.source << "\n\n";

  std::cout << "=== translated SQL ===\n";
  std::cout << sql->ToString() << "\n\n";

  if (program_only) return 0;

  const auto sql_result = SqlExecutor::RunProgram(sql->ToString(), fixture, program.output_def, &program.schema);
  std::cout << "=== DuckDB result (SELECT * FROM \"" << program.output_def << "\") ===\n";
  std::cout << FormatResultSet(sql_result) << "\n";
  WarnIfEmptyResult(std::cerr, "DuckDB", sql_result);

  if (!RelEngine::IsAvailable()) {
    std::cout << "=== Rel engine ===\n";
    std::cout << "(skipped — set REL2SQL_ENABLE_RAICODE=1 and init third_party/raicode)\n";
    return 0;
  }

  const auto rel_result = RelEngine::Run(program.source, fixture, program.output_def, &program.schema);
  std::cout << "=== Rel engine result (out=:" << program.output_def << ") ===\n";
  std::cout << FormatResultSet(rel_result) << "\n";
  WarnIfEmptyResult(std::cerr, "Rel engine", rel_result);

  std::cout << "=== match ===\n";
  if (IsEmpty(rel_result) && IsEmpty(sql_result)) {
    std::cout << "INCONCLUSIVE (both empty — no rows to compare)\n";
    return 4;
  }
  std::cout << (rel_result == sql_result ? "YES\n" : "NO\n");
  return rel_result == sql_result ? 0 : 2;
}
