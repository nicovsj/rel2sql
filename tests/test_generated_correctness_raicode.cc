#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "api/translate.h"
#include "generator/data_fixture.h"
#include "generator/rel_engine.h"
#include "generator/rel_program_generator.h"
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
  return config;
}

struct GenerateTestParam {
  uint64_t seed;
  size_t program_index;
  size_t node_budget;
};

std::string ToString(const GenerateTestParam& param) {
  std::ostringstream os;
  os << "seed" << param.seed << "_idx" << param.program_index << "_budget" << param.node_budget;
  return os.str();
}

struct StaticRelCase {
  std::string id;
  std::string program;
  std::string output_def;
};

std::vector<StaticRelCase> StaticSmokeCases() {
  return {
      {"simple_scalar", "def output {1}", "output"},
      {"simple_set", "def output {\"bar\"; \"foo\"}", "output"},
      {"multi_line_tuple", "def output {(1, 2)}", "output"},
  };
}

void CompareProgramOrSkip(const std::string& program, const RelationMap& edb_map, const DataFixture& fixture,
                          const std::string& output_def) {
  if (!RelEngine::IsAvailable()) {
    GTEST_SKIP() << "RAICode not enabled (init third_party/raicode and set REL2SQL_ENABLE_RAICODE=1)";
  }

  auto sql = GetSQLRel(program, edb_map);
  ASSERT_NE(sql, nullptr) << program;
  const auto sql_result = SqlExecutor::RunProgram(sql->ToString(), fixture, output_def);
  const auto rel_result = RelEngine::Run(program, fixture, output_def);
  EXPECT_EQ(rel_result, sql_result) << "Program:\n" << program;
}

}  // namespace

class RelMatchesSqlTest : public ::testing::TestWithParam<GenerateTestParam> {};

TEST_P(RelMatchesSqlTest, RelMatchesSql) {
  const auto& param = GetParam();
  const auto config = MakeConfig(param.seed, param.program_index, param.node_budget);
  const auto program = GenerateProgram(config);
  const auto fixture = DataFixture::Create(config, program.schema);
  CompareProgramOrSkip(program.source, config.edb_map, fixture, program.output_def);
}

INSTANTIATE_TEST_SUITE_P(DefaultSuite, RelMatchesSqlTest,
                         ::testing::Values(GenerateTestParam{1, 0, 6}, GenerateTestParam{42, 3, 10}),
                         [](const ::testing::TestParamInfo<GenerateTestParam>& info) { return ToString(info.param); });

class StaticRelOracleTest : public ::testing::TestWithParam<StaticRelCase> {};

TEST_P(StaticRelOracleTest, RelMatchesSql) {
  const auto& rel_case = GetParam();
  SuiteConfig config;
  config.seed = 1;
  config.program_index = 0;
  config.node_budget = 6;
  config.edb_map = RelationMap{};
  const auto fixture = DataFixture::Create(config, config.edb_map);
  CompareProgramOrSkip(rel_case.program, config.edb_map, fixture, rel_case.output_def);
}

INSTANTIATE_TEST_SUITE_P(SmokeCases, StaticRelOracleTest, ::testing::ValuesIn(StaticSmokeCases()),
                         [](const ::testing::TestParamInfo<StaticRelCase>& info) { return info.param.id; });

}  // namespace rel2sql::generator
