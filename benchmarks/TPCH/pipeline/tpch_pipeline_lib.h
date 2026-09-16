#ifndef BENCHMARKS_TPCH_PIPELINE_TPCH_PIPELINE_LIB_H_
#define BENCHMARKS_TPCH_PIPELINE_TPCH_PIPELINE_LIB_H_

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "pipeline/manifest_data.h"

namespace rel2sql::tpch_pipeline {

struct CommandResult {
  int exit_code = -1;
  std::string stdout_str;
  std::string stderr_str;
  bool timed_out = false;
};

struct PipelinePaths {
  std::filesystem::path repo_root;
  std::filesystem::path rewrite_script;
  std::filesystem::path edb_file;
  std::filesystem::path rel2sql_bin;
  std::filesystem::path ref_sql_dir;
  std::filesystem::path queries_rel_dir;
};

std::filesystem::path FindRepoRoot();

std::filesystem::path ResolveRel2sqlBin(const std::filesystem::path& repo_root);

PipelinePaths DefaultPaths(const std::filesystem::path& repo_root);

CommandResult RunCommand(const std::vector<std::string>& argv, int timeout_sec, const std::filesystem::path& cwd);

CommandResult RunShell(const std::string& cmd, int timeout_sec, const std::filesystem::path& cwd);

struct RewriteResult {
  bool success = false;
  std::string rel_text;
  std::string error;
};

RewriteResult RunRewrite(int query, const PipelinePaths& paths, bool with_defs = true);

struct TranslateResult {
  bool success = false;
  std::string sql;
  std::string error;
};

TranslateResult RunTranslate(const std::string& rel_text, const QueryManifestEntry& entry, const PipelinePaths& paths);

struct ExecuteResult {
  bool success = false;
  std::string error;
};

ExecuteResult RunExecuteEmpty(const std::string& sql, const PipelinePaths& paths);

struct CompareResult {
  bool success = false;
  std::string message;
  size_t ref_rows = 0;
  size_t gen_rows = 0;
};

// Runs the translated SQL and the reference SQL on a throwaway copy of db_path and compares values
// (never column names -- see ResultSetsEqual). drop_leading_gen_columns discards that many leading
// columns of our own result first, for queries that deliberately project a rank column.
CompareResult RunCompare(int query, const std::string& translated_sql, const std::string& db_path,
                         const PipelinePaths& paths, int drop_leading_gen_columns = 0);

// The SF0.01 database used by the compare stage: $TPCH_DUCKDB_PATH if set, else the default path
// under benchmarks/TPCH/data. nullopt when absent -- these files are gitignored.
std::optional<std::filesystem::path> ResolveComparisonDbPath(const PipelinePaths& paths);

// Run stages 1-3 for one manifest entry; returns human-readable failure reason or nullopt on success.
std::optional<std::string> RunPipelineStages(const QueryManifestEntry& entry, const PipelinePaths& paths);

}  // namespace rel2sql::tpch_pipeline

#endif
