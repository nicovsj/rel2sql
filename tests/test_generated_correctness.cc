#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "api/translate.h"
#include "generator/corpus_case_config.h"
#include "generator/data_fixture.h"
#include "generator/rel_engine_json.h"
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

TEST(GeneratedCorrectnessTest, RejectsArityMismatchApplications) {
  const std::string bad = "def Gen0 {(x, y): B(x, y)}\ndef Gen1 {(x, y): G(x) and Gen0(y)}";
  EXPECT_FALSE(VerifyProgram(bad, CreateDefaultEDBMap()));
}

TEST(GeneratedCorrectnessTest, OutputDefHasConnectedDependencyGraph) {
  const auto profile = FullCorpusProfile();
  for (size_t index = 0; index < 84; ++index) {
    for (size_t budget : {6, 8, 10, 12, 14, 16}) {
      SuiteConfig config = MakeConfig(1, index, budget, profile);
      SCOPED_TRACE("index=" + std::to_string(index) + " budget=" + std::to_string(budget));
      const auto program = GenerateProgram(config);
      const auto expected_output = ProgramOutputDefName(program.source);
      ASSERT_TRUE(expected_output.has_value());
      EXPECT_EQ(program.output_def, *expected_output);
      EXPECT_TRUE(ProgramHasConnectedDependencyGraph(program.source)) << program.source;
    }
  }
}

TEST(GeneratedCorrectnessTest, GeneratedProgramsHaveValidApplicationArity) {
  const auto profile = FullCorpusProfile();
  for (size_t index = 0; index < 84; ++index) {
    for (size_t budget : {6, 8, 10, 12, 14, 16}) {
      SuiteConfig config = MakeConfig(1, index, budget, profile);
      SCOPED_TRACE("index=" + std::to_string(index) + " budget=" + std::to_string(budget));
      const auto program = GenerateProgram(config);
      EXPECT_TRUE(VerifyProgram(program.source, config.edb_map)) << program.source;
    }
  }
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

TEST(GeneratedCorrectnessTest, CanonicalFixtureIsStable) {
  const auto edb_map = CreateDefaultEDBMap();
  const auto first = DataFixture::CreateCanonical(edb_map);
  const auto second = DataFixture::CreateCanonical(edb_map);
  EXPECT_EQ(first.Rows(), second.Rows());
}

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

TEST(GeneratedCorrectnessTest, QuantifierBindingsAreUsedExplicitStrings) {
  EXPECT_TRUE(QuantifierBindingsAreUsed("def Gen0 {(x, y): exists((z) | (A(z) and H(x, y)))}"));
  EXPECT_TRUE(QuantifierBindingsAreUsed("def Gen1 {(x): forall((y in D) | (Gen0(y, x) and Gen0(y, x)))}"));
  EXPECT_TRUE(QuantifierBindingsAreUsed("def Gen2 {(x): (A(x) and exists((z) | G(z)))}"));
  EXPECT_FALSE(QuantifierBindingsAreUsed("exists((w) | ((I(y, x, z) and G(x)) and C(z, x, y)))"));
}

TEST(GeneratedCorrectnessTest, QuantifierBindingsAreUsedInGeneratedPrograms) {
  const std::vector<uint64_t> seeds = {1, 7, 13, 42, 99};
  for (uint64_t seed : seeds) {
    for (size_t index = 0; index < 50; ++index) {
      for (size_t budget : {6, 8, 10, 12, 14}) {
        GeneratorProfile profile;
        profile.allow_forall = true;
        const auto config = MakeConfig(seed, index, budget, profile);
        const auto program = GenerateProgram(config);
        EXPECT_TRUE(QuantifierBindingsAreUsed(program.source)) << program.source;
      }
    }
  }
}

TEST(GeneratedCorrectnessProfileTest, ExtensionalDefsTranslate) {
  GeneratorProfile profile;
  profile.allow_extensional = true;
  const auto config = MakeConfig(7, 2, 8, profile);
  const auto program = GenerateProgram(config);
  auto sql = GetSQLRel(program.source, config.edb_map);
  ASSERT_NE(sql, nullptr) << program.source;
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

TEST(GeneratedCorrectnessTest, ForallBindingsHaveDomainsExplicitStrings) {
  EXPECT_TRUE(ForallBindingsHaveDomains("def Gen0 {(x): forall((y in D) | B(x, y))}"));
  EXPECT_TRUE(ForallBindingsHaveDomains("def Gen0 {(x): forall((y in A, z in D) | C(x, y, z))}"));
  EXPECT_FALSE(ForallBindingsHaveDomains("def Gen0 {(x): forall((y) | B(x, y))}"));
}

TEST(GeneratedCorrectnessProfileTest, QuantifiersPresetGeneratesForallWithDomains) {
  bool saw_forall = false;
  for (size_t index = 0; index < 20; ++index) {
    for (size_t budget : {8, 10, 12, 14}) {
      const auto config = MakeConfig(3, index, budget, QuantifiersCorpusProfile());
      const auto program = GenerateProgram(config);
      EXPECT_TRUE(ForallBindingsHaveDomains(program.source)) << program.source;
      if (program.source.find("forall((") != std::string::npos) saw_forall = true;
      auto sql = GetSQLRel(program.source, config.edb_map);
      ASSERT_NE(sql, nullptr) << program.source;
    }
  }
  EXPECT_TRUE(saw_forall);
}

TEST(GeneratedCorrectnessProfileTest, ExprAlgebraPresetGeneratesProductOrUnion) {
  for (size_t index = 0; index < 20; ++index) {
    const auto config = MakeConfig(5, index, 10, ExprAlgebraCorpusProfile());
    const auto program = GenerateProgram(config);
    const bool has_expr_algebra =
        program.source.find(';') != std::string::npos || program.source.find('[') != std::string::npos ||
        (program.source.find('(') != std::string::npos && program.source.find(',') != std::string::npos);
    EXPECT_TRUE(has_expr_algebra) << program.source;
    auto sql = GetSQLRel(program.source, config.edb_map);
    ASSERT_NE(sql, nullptr) << program.source;
  }
}

TEST(GeneratedCorrectnessProfileTest, AggregatesPresetGeneratesAggregates) {
  for (size_t index = 0; index < 20; ++index) {
    const auto config = MakeConfig(9, index, 10, AggregatesCorpusProfile());
    const auto program = GenerateProgram(config);
    EXPECT_NE(program.source.find('['), std::string::npos) << program.source;
    auto sql = GetSQLRel(program.source, config.edb_map);
    ASSERT_NE(sql, nullptr) << program.source;
  }
}

TEST(GeneratedCorrectnessTest, CorpusProfilePresetsAreRegistered) {
  const auto presets = CorpusProfilePresets();
  ASSERT_EQ(presets.size(), 4u);
  EXPECT_EQ(presets[0].name, "full");
  EXPECT_EQ(presets[1].name, "quantifiers");
  EXPECT_EQ(presets[2].name, "expr_algebra");
  EXPECT_EQ(presets[3].name, "aggregates");
}

TEST(GeneratedCorrectnessTest, NoStaticallyFalseComparisonsInFullProfile) {
  EXPECT_TRUE(ProgramContainsStaticallyFalseComparison("def Gen3 {(x, y, z): (C(x, y, z) and z - z = 1)}"));
  EXPECT_FALSE(ProgramContainsStaticallyFalseComparison("def Gen3 {(x, y, z): (C(x, y, z) and z - z = 0)}"));
  EXPECT_FALSE(ProgramContainsStaticallyFalseComparison("def Gen3 {(x, y, z): (C(x, y, z) and x - y = 1)}"));

  for (size_t index = 0; index < 84; ++index) {
    for (size_t budget : {6, 8, 10, 12, 14}) {
      const auto config = MakeConfig(1, index, budget, FullCorpusProfile());
      const auto program = GenerateProgram(config);
      EXPECT_FALSE(ProgramContainsStaticallyFalseComparison(program.source)) << program.source;
    }
  }
}

TEST(GeneratedCorrectnessTest, RelEngineJsonErrorIncludesCompilerReport) {
  const std::string json =
      R"({"error":"-- TYPE_PROBLEM ----\nerror: arity mismatch\n\nKeyError: key :Gen3 not found","problems":["-- TYPE_PROBLEM ----\nerror: arity mismatch"]})";
  try {
    (void)ParseRelEngineResponseJson(json);
    FAIL() << "expected parse to throw";
  } catch (const std::exception& ex) {
    const std::string message = ex.what();
    EXPECT_NE(message.find("TYPE_PROBLEM"), std::string::npos) << message;
    EXPECT_NE(message.find("KeyError"), std::string::npos) << message;
  }
}

TEST(GeneratedCorrectnessTest, ChainedDefsDoNotRepeatVarsInIdbAtoms) {
  for (size_t index = 0; index < 84; ++index) {
    for (size_t budget : {6, 8, 10, 12, 14, 16}) {
      const auto config = MakeConfig(1, index, budget, FullCorpusProfile());
      const auto program = GenerateProgram(config);
      EXPECT_EQ(program.source.find("Gen0(x, x)"), std::string::npos) << program.source;
    }
  }
}

TEST(GeneratedCorrectnessTest, SingleDefOutputIsGen0) {
  for (size_t index = 0; index < 84; ++index) {
    for (size_t budget : {6, 8, 10, 12, 14, 16}) {
      const auto config = MakeConfig(1, index, budget, FullCorpusProfile());
      const auto program = GenerateProgram(config);
      EXPECT_EQ(program.output_def, "Gen0") << program.source;
      EXPECT_EQ(program.source.find("def Gen1"), std::string::npos) << program.source;
    }
  }
}

TEST(GeneratedCorrectnessProfileTest, FullCorpusProfileTranslates) {
  const auto config = MakeConfig(17, 3, 12, FullCorpusProfile());
  const auto program = GenerateProgram(config);
  EXPECT_TRUE(QuantifierBindingsAreUsed(program.source)) << program.source;
  auto sql = GetSQLRel(program.source, config.edb_map);
  ASSERT_NE(sql, nullptr) << program.source;
}

}  // namespace rel2sql::generator
