#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "api/translate.h"
#include "generator/data_fixture.h"
#include "generator/rel_engine.h"
#include "generator/rel_program_generator.h"
#include "generator/sql_executor.h"
#include "test_common.h"

namespace rel2sql::generator {
namespace {

SuiteConfig MakeConfig(uint64_t seed, size_t program_index, size_t node_budget,
                       const GeneratorProfile& profile = GeneratorProfile{}) {
  SuiteConfig config;
  config.seed = seed;
  config.program_index = program_index;
  config.node_budget = node_budget;
  config.edb_map = CreateDefaultEDBMap();
  config.profile = profile;
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

std::vector<GenerateTestParam> BuildDefaultParams() {
  std::vector<GenerateTestParam> params;
  const std::vector<uint64_t> seeds = {1, 42, 9001};
  const std::vector<size_t> budgets = {6, 12};
  for (uint64_t seed : seeds) {
    for (size_t index = 0; index < 20; ++index) {
      for (size_t budget : budgets) {
        params.push_back({seed, index, budget});
      }
    }
  }
  return params;
}

}  // namespace

TEST(GeneratedCorrectnessTest, GenerateProgramIsDeterministic) {
  const auto config = MakeConfig(42, 7, 10);
  const auto first = GenerateProgram(config);
  const auto second = GenerateProgram(config);
  EXPECT_EQ(first.source, second.source);
  EXPECT_EQ(first.output_def, second.output_def);
}

TEST(GeneratedCorrectnessTest, GenerateProgramTranslates) {
  const auto config = MakeConfig(1, 0, 8);
  const auto program = GenerateProgram(config);
  EXPECT_FALSE(program.source.empty());
  EXPECT_TRUE(VerifyProgram(program.source, config.edb_map));
  auto sql = GetSQLRel(program.source, config.edb_map);
  ASSERT_NE(sql, nullptr);
  EXPECT_FALSE(sql->ToString().empty());
}

class GenerateProgramParamTest : public ::testing::TestWithParam<GenerateTestParam> {};

TEST_P(GenerateProgramParamTest, TranslatesSuccessfully) {
  const auto& param = GetParam();
  const auto config = MakeConfig(param.seed, param.program_index, param.node_budget);
  const auto program = GenerateProgram(config);
  auto sql = GetSQLRel(program.source, config.edb_map);
  ASSERT_NE(sql, nullptr) << "Program:\n" << program.source;
}

INSTANTIATE_TEST_SUITE_P(DefaultSuite, GenerateProgramParamTest, ::testing::ValuesIn(BuildDefaultParams()),
                         [](const ::testing::TestParamInfo<GenerateTestParam>& info) { return ToString(info.param); });

TEST(GeneratedCorrectnessTest, DataFixtureIsDeterministic) {
  const auto config = MakeConfig(99, 3, 10);
  const auto program = GenerateProgram(config);
  const auto first = DataFixture::Create(config, program.schema);
  const auto second = DataFixture::Create(config, program.schema);
  ASSERT_EQ(first.Rows().size(), second.Rows().size());
  for (const auto& kv : first.Rows()) {
    ASSERT_TRUE(second.Rows().count(kv.first));
    EXPECT_EQ(kv.second, second.Rows().at(kv.first));
  }
}

class DuckDbGeneratedTest : public ::testing::TestWithParam<GenerateTestParam> {};

TEST_P(DuckDbGeneratedTest, GeneratedProgramExecutesOnDuckDB) {
  const auto& param = GetParam();
  const auto config = MakeConfig(param.seed, param.program_index, param.node_budget);
  const auto program = GenerateProgram(config);
  const auto fixture = DataFixture::Create(config, program.schema);
  auto sql = GetSQLRel(program.source, config.edb_map);
  ASSERT_NE(sql, nullptr) << program.source;
  const auto result = SqlExecutor::RunProgram(sql->ToString(), fixture, program.output_def);
  (void)result;
  SUCCEED();
}

INSTANTIATE_TEST_SUITE_P(DefaultSuite, DuckDbGeneratedTest,
                         ::testing::Values(GenerateTestParam{1, 0, 6}, GenerateTestParam{42, 5, 8},
                                           GenerateTestParam{9001, 11, 12}),
                         [](const ::testing::TestParamInfo<GenerateTestParam>& info) { return ToString(info.param); });

class RelMatchesSqlTest : public ::testing::TestWithParam<GenerateTestParam> {};

TEST_P(RelMatchesSqlTest, RelMatchesSql) {
  if (!RelEngine::IsAvailable()) {
    GTEST_SKIP() << "Rel engine not configured (set REL2SQL_REL_ENGINE)";
  }

  const auto& param = GetParam();
  const auto config = MakeConfig(param.seed, param.program_index, param.node_budget);
  const auto program = GenerateProgram(config);
  const auto fixture = DataFixture::Create(config, program.schema);

  auto sql = GetSQLRel(program.source, config.edb_map);
  ASSERT_NE(sql, nullptr) << program.source;
  const auto sql_result = SqlExecutor::RunProgram(sql->ToString(), fixture, program.output_def);

  const auto rel_result = RelEngine::Run(program.source, fixture, program.output_def);
  EXPECT_EQ(rel_result, sql_result);
}

INSTANTIATE_TEST_SUITE_P(DefaultSuite, RelMatchesSqlTest,
                         ::testing::Values(GenerateTestParam{1, 0, 6}, GenerateTestParam{42, 3, 10}),
                         [](const ::testing::TestParamInfo<GenerateTestParam>& info) { return ToString(info.param); });

TEST(GeneratedCorrectnessProfileTest, ExtensionalDefsTranslate) {
  GeneratorProfile profile;
  profile.allow_extensional = true;
  const auto config = MakeConfig(7, 2, 8, profile);
  const auto program = GenerateProgram(config);
  auto sql = GetSQLRel(program.source, config.edb_map);
  ASSERT_NE(sql, nullptr) << program.source;
}

TEST(GeneratedCorrectnessProfileTest, RecursionTranslates) {
  GeneratorProfile profile;
  profile.allow_recursion = true;
  profile.max_defs = 2;
  const auto config = MakeConfig(13, 4, 12, profile);
  const auto program = GenerateProgram(config);
  auto sql = GetSQLRel(program.source, config.edb_map);
  ASSERT_NE(sql, nullptr) << program.source;
  EXPECT_NE(sql->ToString().find("RECURSIVE"), std::string::npos) << sql->ToString();
}

TEST(GeneratedCorrectnessProfileTest, AggregatesTranslate) {
  GeneratorProfile profile;
  profile.allow_aggregates = true;
  const auto config = MakeConfig(21, 1, 10, profile);
  const auto program = GenerateProgram(config);
  auto sql = GetSQLRel(program.source, config.edb_map);
  ASSERT_NE(sql, nullptr) << program.source;
}

TEST(GeneratedCorrectnessProfileTest, PartialAppTranslates) {
  GeneratorProfile profile;
  profile.allow_partial_app = true;
  const auto config = MakeConfig(31, 6, 10, profile);
  const auto program = GenerateProgram(config);
  auto sql = GetSQLRel(program.source, config.edb_map);
  ASSERT_NE(sql, nullptr) << program.source;
}

TEST(GeneratedCorrectnessProfileTest, ProductsTranslate) {
  GeneratorProfile profile;
  profile.allow_products = true;
  const auto config = MakeConfig(41, 8, 10, profile);
  const auto program = GenerateProgram(config);
  auto sql = GetSQLRel(program.source, config.edb_map);
  ASSERT_NE(sql, nullptr) << program.source;
}

}  // namespace rel2sql::generator
