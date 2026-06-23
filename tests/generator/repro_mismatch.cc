// Reproduce a corpus mismatch case by id (from mismatches.jsonl).
// Usage: repro_mismatch s1_i0_b10_pfull

#include <iostream>
#include <sstream>
#include <string>

#include "api/translate.h"
#include "generator/corpus_case_config.h"
#include "generator/data_fixture.h"
#include "generator/rel_engine.h"
#include "generator/rel_engine_client.h"
#include "generator/rel_program_generator.h"
#include "generator/result_set.h"
#include "generator/sql_executor.h"

namespace rel2sql::generator {
namespace {

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

}  // namespace
}  // namespace rel2sql::generator

int main(int argc, char** argv) {
  using namespace rel2sql::generator;

  if (argc < 2) {
    std::cerr << "Usage: repro_mismatch <case_id>\n";
    std::cerr << "Example: repro_mismatch s1_i0_b10_pfull\n";
    return 1;
  }

  const auto parsed = ParseCaseId(argv[1]);
  if (!parsed) {
    std::cerr << "invalid case id: " << argv[1] << "\n";
    return 1;
  }

  const auto config = SuiteConfigFromCaseId(*parsed);
  const auto program = GenerateProgram(config);
  const auto fixture = DataFixture::CreateCanonical(config.edb_map);

  auto sql = GetSQLRel(program.source, config.edb_map);
  if (!sql) {
    std::cerr << "translation failed\n";
    return 1;
  }

  std::cout << "=== case " << argv[1] << " ===\n";
  std::cout << "output_def=" << program.output_def << "\n\n";
  std::cout << "=== Rel program ===\n" << program.source << "\n\n";
  std::cout << "=== SQL ===\n" << sql->ToString() << "\n\n";

  if (!RelEngine::IsAvailable()) {
    std::cerr << "RelEngine not available (set REL2SQL_ENABLE_RAICODE=1, init third_party/raicode)\n";
    return 1;
  }

  if (!RelEngine::IsServerReachable()) {
    std::cerr << "error: no warm Rel engine server.\n";
    if (auto path = RelEngineClient::DefaultSocketPath()) {
      std::cerr << "  expected socket: " << *path << "\n";
    }
    std::cerr << "Start one and wait until ready:\n";
    std::cerr << "  task rel-engine:start\n";
    return 1;
  }

  const auto rel_result = RelEngine::Run(program.source, fixture, program.output_def, &program.schema);
  std::cout << "=== Rel result ===\n" << FormatResultSet(rel_result) << "\n";
  WarnIfEmptyResult(std::cerr, "Rel engine", rel_result);

  try {
    const auto sql_result = SqlExecutor::RunProgram(sql->ToString(), fixture, program.output_def, &program.schema);
    std::cout << "=== SQL result ===\n" << FormatResultSet(sql_result) << "\n";
    WarnIfEmptyResult(std::cerr, "SQL", sql_result);
    if (IsEmpty(rel_result) && IsEmpty(sql_result)) {
      std::cout << "=== match ===\nINCONCLUSIVE (both empty — no rows to compare)\n";
      return 4;
    }
    std::cout << "=== match ===\n" << (rel_result == sql_result ? "YES\n" : "NO (translation bug)\n");
    return rel_result == sql_result ? 0 : 2;
  } catch (const std::exception& e) {
    std::cout << "=== SQL result ===\n";
    std::cout << "EXEC ERROR: " << e.what() << "\n";
    std::cout << "=== match ===\nNO (invalid or wrong SQL)\n";
    return 3;
  }
}
