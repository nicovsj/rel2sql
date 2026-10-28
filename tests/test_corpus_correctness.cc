#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "api/translate.h"
#include "generator/corpus.h"
#include "generator/corpus_version.h"
#include "generator/sql_executor.h"
#include "test_common.h"

#ifndef REL2SQL_CORPUS_VERSION
#define REL2SQL_CORPUS_VERSION 1
#endif

namespace rel2sql::generator {
namespace {

constexpr int kCorpusVersion = REL2SQL_CORPUS_VERSION;

std::string FormatResultSet(const ResultSet& rs) {
  std::ostringstream os;
  os << "columns: ";
  for (size_t i = 0; i < rs.column_names.size(); ++i) {
    if (i > 0) os << ", ";
    os << rs.column_names[i];
  }
  os << "\nrows:\n";
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

void RunCorpusCase(const CorpusCase& corpus_case) {
  auto sql = GetSQLRel(corpus_case.program, CreateDefaultEDBMap());
  ASSERT_NE(sql, nullptr) << "case " << corpus_case.id << "\nProgram:\n" << corpus_case.program;

  const auto fixture = DataFixtureFromCorpus(corpus_case);
  const auto sql_result = SqlExecutor::RunProgram(sql->ToString(), fixture, corpus_case.output_def);
  // An empty Rel answer has no columns, so only its rows are compared.
  const bool equal = corpus_case.expected.rows.empty() ? sql_result.rows.empty() : sql_result == corpus_case.expected;
  EXPECT_TRUE(equal) << "case " << corpus_case.id << "\nProgram:\n"
                     << corpus_case.program << "\nSQL:\n"
                     << sql->ToString() << "\nExpected:\n"
                     << FormatResultSet(corpus_case.expected) << "\nActual:\n"
                     << FormatResultSet(sql_result);
}

std::vector<std::string> ShardNamesForTests() {
  const auto corpus_root = CorpusRoot(kCorpusVersion);
  const auto manifest_path = corpus_root / "manifest.json";
  if (!std::filesystem::exists(manifest_path)) {
    return {};
  }
  const auto manifest = LoadManifest(manifest_path);
  return manifest.shards;
}

}  // namespace

TEST(CorpusCorrectnessTest, ManifestFingerprintMatches) {
  const auto manifest_path = CorpusRoot(kCorpusVersion) / "manifest.json";
  if (!std::filesystem::exists(manifest_path)) {
    GTEST_SKIP() << "corpus not built yet";
  }
  const auto manifest = LoadManifest(manifest_path);
  EXPECT_EQ(manifest.generator_fingerprint,
            kCorpusVersion == 1 ? kCorpusGeneratorFingerprint : kCorpusV2GeneratorFingerprint);
  EXPECT_GE(manifest.case_count, 1u);
  EXPECT_FALSE(manifest.shards.empty());
}

class CorpusShardTest : public ::testing::TestWithParam<std::string> {};

TEST_P(CorpusShardTest, SqlMatchesGolden) {
  const auto corpus_root = CorpusRoot(kCorpusVersion);
  const auto shard_path = corpus_root / GetParam();
  ASSERT_TRUE(std::filesystem::exists(shard_path)) << shard_path;

  const auto cases = LoadShard(shard_path);
  ASSERT_FALSE(cases.empty()) << GetParam();
  for (const auto& corpus_case : cases) {
    RunCorpusCase(corpus_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Shards, CorpusShardTest, ::testing::ValuesIn(ShardNamesForTests()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                           std::string name = info.param;
                           if (name.size() >= 6 && name.substr(name.size() - 6) == ".jsonl") {
                             name = name.substr(0, name.size() - 6);
                           }
                           return name;
                         });

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CorpusShardTest);

}  // namespace rel2sql::generator
