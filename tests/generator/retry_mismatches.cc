// Re-evaluates cases listed in mismatches.jsonl; promotes passing cases into corpus shards.
// Usage: retry_mismatches [--output-dir PATH] [--shard-size N]

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "api/translate.h"
#include "generator/corpus.h"
#include "generator/corpus_case_config.h"
#include "generator/corpus_version.h"
#include "generator/data_fixture.h"
#include "generator/rel_engine.h"
#include "generator/rel_program_generator.h"
#include "generator/result_set.h"
#include "generator/sql_executor.h"
#include "test_common.h"

namespace rel2sql::generator {
namespace {

struct MismatchEntry {
  std::string id;
  std::string reason;
};

struct RetryOptions {
  std::filesystem::path output_dir = CorpusV1Root();
  size_t shard_size = 150;
};

RetryOptions ParseArgs(int argc, char** argv) {
  RetryOptions opts;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--output-dir" && i + 1 < argc) {
      opts.output_dir = argv[++i];
    } else if (arg == "--shard-size" && i + 1 < argc) {
      opts.shard_size = static_cast<size_t>(std::stoull(argv[++i]));
    }
  }
  return opts;
}

std::optional<std::string> ParseJsonStringField(const std::string& line, const std::string& field) {
  const std::string needle = "\"" + field + "\":\"";
  const size_t pos = line.find(needle);
  if (pos == std::string::npos) return std::nullopt;
  const size_t start = pos + needle.size();
  const size_t end = line.find('"', start);
  if (end == std::string::npos) return std::nullopt;
  return line.substr(start, end - start);
}

std::vector<MismatchEntry> LoadMismatches(const std::filesystem::path& path) {
  std::vector<MismatchEntry> entries;
  std::ifstream in(path);
  if (!in) return entries;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    auto id = ParseJsonStringField(line, "id");
    auto reason = ParseJsonStringField(line, "reason");
    if (!id || !reason) continue;
    entries.push_back({*id, *reason});
  }
  return entries;
}

std::set<std::string> LoadExistingIds(const std::filesystem::path& output_dir) {
  std::set<std::string> ids;
  if (!std::filesystem::exists(output_dir)) return ids;
  for (const auto& entry : std::filesystem::directory_iterator(output_dir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".jsonl") continue;
    const auto name = entry.path().filename().string();
    if (!name.starts_with("shard_")) continue;
    std::ifstream in(entry.path());
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      try {
        ids.insert(ParseCorpusCaseLine(line).id);
      } catch (...) {
      }
    }
  }
  return ids;
}

void WriteManifest(const std::filesystem::path& output_dir, size_t case_count) {
  std::vector<std::string> shard_names;
  for (const auto& entry : std::filesystem::directory_iterator(output_dir)) {
    if (!entry.is_regular_file()) continue;
    const auto name = entry.path().filename().string();
    if (name.starts_with("shard_") && name.ends_with(".jsonl")) {
      shard_names.push_back(name);
    }
  }
  std::sort(shard_names.begin(), shard_names.end());

  std::ostringstream os;
  os << "{\n";
  os << "  \"corpus_version\": 1,\n";
  os << "  \"generator_fingerprint\": \"" << kCorpusGeneratorFingerprint << "\",\n";
  os << "  \"edb_map\": \"default\",\n";
  os << "  \"case_count\": " << case_count << ",\n";
  os << "  \"shard_count\": " << shard_names.size() << "\n";
  os << "}\n";
  std::ofstream out(output_dir / "manifest.json");
  out << os.str();
}

std::optional<std::string> EvaluateCase(const std::string& case_id, CorpusCase* out_case) {
  const auto parsed = ParseCaseId(case_id);
  if (!parsed) return "invalid_case_id";

  const auto config = SuiteConfigFromCaseId(*parsed);
  GeneratedProgram program;
  try {
    program = GenerateProgram(config);
  } catch (...) {
    return "generate_failed";
  }

  const auto fixture = DataFixture::CreateCanonical(config.edb_map);
  ResultSet expected;
  try {
    expected = RelEngine::Run(program.source, fixture, program.output_def, &program.schema);
  } catch (...) {
    return "rel_exec_error";
  }
  if (IsEmpty(expected)) return "empty_oracle";

  auto sql = GetSQLRel(program.source, config.edb_map);
  if (!sql) return "translation_failed";

  try {
    const auto sql_result = SqlExecutor::RunProgram(sql->ToString(), fixture, program.output_def, &program.schema);
    if (IsEmpty(sql_result)) return "empty_sql";
    if (sql_result != expected) return "sql_mismatch";
  } catch (...) {
    return "sql_exec_error";
  }

  out_case->id = case_id;
  out_case->config = config;
  out_case->program = program.source;
  out_case->output_def = program.output_def;
  out_case->schema = program.schema;
  out_case->edb = fixture.Rows();
  out_case->expected = expected;
  return std::nullopt;
}

}  // namespace
}  // namespace rel2sql::generator

int main(int argc, char** argv) {
  using namespace rel2sql::generator;

  const auto opts = ParseArgs(argc, argv);
  if (!RelEngine::IsAvailable()) {
    std::cerr << "RAICode not available. Start rel-engine server and set REL2SQL_ENABLE_RAICODE=1\n";
    return 1;
  }
  if (!RelEngine::IsServerReachable()) {
    std::cerr << "error: no warm Rel engine server. Run: task rel-engine:ensure\n";
    return 1;
  }

  const auto mismatch_path = opts.output_dir / "mismatches.jsonl";
  const auto entries = LoadMismatches(mismatch_path);
  if (entries.empty()) {
    std::cout << "no mismatches to retry in " << mismatch_path << "\n";
    return 0;
  }

  std::set<std::string> existing_ids = LoadExistingIds(opts.output_dir);
  std::set<std::string> seen_programs = LoadExistingPrograms(opts.output_dir);
  const size_t initial_case_count = existing_ids.size();

  std::vector<std::string> shard_names;
  for (const auto& entry : std::filesystem::directory_iterator(opts.output_dir)) {
    if (!entry.is_regular_file()) continue;
    const auto name = entry.path().filename().string();
    if (name.starts_with("shard_")) shard_names.push_back(name);
  }
  std::sort(shard_names.begin(), shard_names.end());

  size_t shard_index = shard_names.size();
  size_t shard_line_count = 0;
  std::ofstream shard_out;

  auto open_next_shard = [&]() {
    if (shard_out.is_open()) shard_out.close();
    std::ostringstream name;
    name << "shard_" << std::setw(3) << std::setfill('0') << shard_index << ".jsonl";
    shard_names.push_back(name.str());
    shard_out.open(opts.output_dir / name.str());
    shard_line_count = 0;
    ++shard_index;
  };

  if (!shard_names.empty()) {
    const auto last_shard = opts.output_dir / shard_names.back();
    size_t lines = 0;
    std::ifstream count_in(last_shard);
    std::string line;
    while (std::getline(count_in, line)) {
      if (!line.empty()) ++lines;
    }
    shard_line_count = lines % opts.shard_size;
    if (shard_line_count >= opts.shard_size) {
      open_next_shard();
    } else {
      shard_out.open(last_shard, std::ios::app);
    }
  } else {
    open_next_shard();
  }

  size_t promoted = 0;
  size_t still_failing = 0;
  size_t skipped = 0;
  size_t skipped_duplicate = 0;
  std::vector<MismatchEntry> remaining;

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    if (existing_ids.count(entry.id)) {
      ++skipped;
      continue;
    }

    CorpusCase corpus_case;
    const auto failure = EvaluateCase(entry.id, &corpus_case);
    if (failure) {
      remaining.push_back({entry.id, *failure});
      ++still_failing;
      if ((i + 1) % 25 == 0) {
        std::cerr << "retried " << (i + 1) << "/" << entries.size() << " (" << promoted << " promoted)\n";
      }
      continue;
    }

    if (seen_programs.count(corpus_case.program)) {
      ++skipped_duplicate;
      continue;
    }

    if (shard_line_count >= opts.shard_size) {
      open_next_shard();
    }
    shard_out << SerializeCorpusCaseLine(corpus_case) << "\n";
    ++shard_line_count;
    existing_ids.insert(entry.id);
    seen_programs.insert(corpus_case.program);
    ++promoted;

    if ((i + 1) % 25 == 0) {
      std::cerr << "retried " << (i + 1) << "/" << entries.size() << " (" << promoted << " promoted)\n";
    }
  }

  if (shard_out.is_open()) shard_out.close();

  {
    std::ofstream out(mismatch_path);
    for (const auto& entry : remaining) {
      out << "{\"id\":\"" << entry.id << "\",\"reason\":\"" << entry.reason << "\"}\n";
    }
  }

  WriteManifest(opts.output_dir, existing_ids.size());

  std::cout << "retry complete: promoted " << promoted << " of " << entries.size() << " mismatches\n";
  std::cout << "still failing: " << still_failing << ", skipped (already in corpus): " << skipped
            << ", skipped duplicate program: " << skipped_duplicate << "\n";
  std::cout << "corpus cases: " << initial_case_count << " -> " << existing_ids.size() << "\n";
  std::cout << "mismatches: " << entries.size() << " -> " << remaining.size() << "\n";
  return 0;
}
