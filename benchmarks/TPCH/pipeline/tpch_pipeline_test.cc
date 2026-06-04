#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "gtest/gtest.h"
#include "pipeline/manifest_data.h"
#include "pipeline/tpch_pipeline_lib.h"

namespace rel2sql::tpch_pipeline {
namespace {

PipelinePaths TestPaths() { return DefaultPaths(FindRepoRoot()); }

class TpchPipelineTest : public ::testing::TestWithParam<QueryManifestEntry> {};

TEST_P(TpchPipelineTest, ManifestStages) {
  const auto& entry = GetParam();
  auto paths = TestPaths();
  ASSERT_TRUE(std::filesystem::exists(paths.rewrite_script)) << paths.rewrite_script;
  ASSERT_TRUE(std::filesystem::exists(paths.edb_file)) << paths.edb_file;
  ASSERT_TRUE(std::filesystem::exists(paths.rel2sql_bin)) << paths.rel2sql_bin;

  auto failure = RunPipelineStages(entry, paths);
  EXPECT_FALSE(failure.has_value()) << *failure;
}

INSTANTIATE_TEST_SUITE_P(AllQueries, TpchPipelineTest, ::testing::ValuesIn(kManifestEntries),
                         [](const ::testing::TestParamInfo<QueryManifestEntry>& info) {
                           return "Q" + std::to_string(info.param.query);
                         });

}  // namespace
}  // namespace rel2sql::tpch_pipeline
