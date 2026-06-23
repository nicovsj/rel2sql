// Builds the golden Rel-oracle corpus via the persistent Rel engine server.
// Usage: build_corpus [--output-dir PATH] [--shard-size N] [--resume] [--max-cases N]

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
#include "generator/rel_engine_json.h"
#include "generator/rel_program_generator.h"
#include "generator/result_set.h"
#include "generator/sql_executor.h"
#include "test_common.h"

namespace rel2sql::generator {
namespace {

struct BuildOptions {
  std::filesystem::path output_dir = CorpusV1Root();
  size_t shard_size = 150;
  bool resume = false;
  std::optional<size_t> max_cases;
};

BuildOptions ParseArgs(int argc, char** argv) {
  BuildOptions opts;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--resume") {
      opts.resume = true;
    } else if (arg == "--output-dir" && i + 1 < argc) {
      opts.output_dir = argv[++i];
    } else if (arg == "--shard-size" && i + 1 < argc) {
      opts.shard_size = static_cast<size_t>(std::stoull(argv[++i]));
    } else if (arg == "--max-cases" && i + 1 < argc) {
      opts.max_cases = static_cast<size_t>(std::stoull(argv[++i]));
    }
  }
  return opts;
}

std::string TruncateForLog(std::string_view text, size_t max_len = 500) {
  if (text.size() <= max_len) return std::string(text);
  return std::string(text.substr(0, max_len)) + "...";
}

void WriteRelSkipMismatch(std::ostream& out, const std::string& case_id, const std::string& error,
                          const std::string& program) {
  out << "{\"id\":\"" << case_id << "\",\"reason\":\"rel_exec_error\",\"error\":\"" << JsonEscape(error)
      << "\",\"program\":\"" << JsonEscape(TruncateForLog(program)) << "\"}\n";
}

void LogRelSkip(std::ostream& out, const std::string& case_id, const std::string& error, const std::string& program) {
  out << "skip rel " << case_id << ":\n";
  std::istringstream lines(error);
  std::string line;
  while (std::getline(lines, line)) {
    out << "  " << line << "\n";
  }
  out << "  program: " << TruncateForLog(program) << "\n";
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

void WriteManifest(const std::filesystem::path& output_dir, size_t case_count, const std::vector<std::string>& shards) {
  (void)shards;
  const auto shard_names = [&]() {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(output_dir)) {
      if (!entry.is_regular_file()) continue;
      const auto name = entry.path().filename().string();
      if (name.starts_with("shard_") && name.ends_with(".jsonl")) {
        names.push_back(name);
      }
    }
    std::sort(names.begin(), names.end());
    return names;
  }();

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
    std::cerr << "rel engine server not reachable — start with: task rel-engine:start\n";
    return 1;
  }

  const size_t grid_slots = CorpusProfilePresets().size() * CorpusBuildSeeds().size() *
                            CorpusBuildProgramIndices().size() * CorpusBuildBudgets().size();
  std::cerr << "corpus build starting (fingerprint=" << kCorpusGeneratorFingerprint << ", grid=" << grid_slots
            << " slots)\n";
  std::cerr << "first RAICode query often takes 2-3 minutes; watch progress in .rel_engine.log\n" << std::flush;

  std::filesystem::create_directories(opts.output_dir);

  if (!opts.resume) {
    for (const auto& entry : std::filesystem::directory_iterator(opts.output_dir)) {
      if (entry.path().extension() == ".jsonl") {
        const auto name = entry.path().filename().string();
        if (name.starts_with("shard_")) {
          std::filesystem::remove(entry.path());
        }
      }
    }
  }

  const auto seeds = CorpusBuildSeeds();
  const auto program_indices = CorpusBuildProgramIndices();
  const auto budgets = CorpusBuildBudgets();
  const auto canonical_fixture = DataFixture::CreateCanonical(rel2sql::CreateDefaultEDBMap());

  std::set<std::string> existing_ids;
  std::set<std::string> seen_programs;
  if (opts.resume) {
    existing_ids = LoadExistingIds(opts.output_dir);
    seen_programs = LoadExistingPrograms(opts.output_dir);
    std::cerr << "resume: skipping " << existing_ids.size() << " existing cases (" << seen_programs.size()
              << " unique programs)\n";
  }

  std::vector<std::string> shard_names;
  size_t shard_index = 0;
  size_t shard_line_count = 0;
  size_t total_cases = existing_ids.size();
  std::ofstream shard_out;

  auto open_next_shard = [&]() {
    if (shard_out.is_open()) shard_out.close();
    std::ostringstream name;
    name << "shard_" << std::setw(3) << std::setfill('0') << shard_index << ".jsonl";
    const std::string shard_name = name.str();
    shard_names.push_back(shard_name);
    shard_out.open(opts.output_dir / shard_name);
    shard_line_count = 0;
    ++shard_index;
  };

  if (opts.resume && !existing_ids.empty()) {
    for (const auto& entry : std::filesystem::directory_iterator(opts.output_dir)) {
      if (entry.path().extension() == ".jsonl") {
        const auto name = entry.path().filename().string();
        if (name.starts_with("shard_")) {
          shard_names.push_back(name);
        }
      }
    }
    std::sort(shard_names.begin(), shard_names.end());
    if (!shard_names.empty()) {
      const auto last_shard = opts.output_dir / shard_names.back();
      size_t lines = 0;
      std::ifstream count_in(last_shard);
      std::string line;
      while (std::getline(count_in, line)) {
        if (!line.empty()) ++lines;
      }
      shard_line_count = lines % opts.shard_size;
      shard_index = shard_names.size();
      if (shard_line_count >= opts.shard_size) {
        open_next_shard();
      } else {
        shard_out.open(last_shard, std::ios::app);
      }
    } else {
      open_next_shard();
    }
  } else {
    open_next_shard();
  }

  size_t skipped_generate = 0;
  size_t skipped_rel = 0;
  size_t skipped_mismatch = 0;
  size_t skipped_empty = 0;
  size_t skipped_duplicate = 0;
  size_t cases_attempted = 0;
  std::ofstream mismatch_out;

  if (!opts.resume) {
    mismatch_out.open(opts.output_dir / "mismatches.jsonl");
  } else {
    mismatch_out.open(opts.output_dir / "mismatches.jsonl", std::ios::app);
  }

  for (const auto& preset : CorpusProfilePresets()) {
    for (uint64_t seed : seeds) {
      for (size_t index : program_indices) {
        for (size_t budget : budgets) {
          if (opts.max_cases && total_cases >= *opts.max_cases) goto done;

          const std::string case_id = MakeCaseId(seed, index, budget, preset.name);
          if (existing_ids.count(case_id)) continue;

          SuiteConfig config;
          config.seed = seed;
          config.program_index = index;
          config.node_budget = budget;
          config.edb_map = rel2sql::CreateDefaultEDBMap();
          config.profile = preset.profile;

          GeneratedProgram program;
          try {
            program = GenerateProgram(config);
          } catch (...) {
            ++skipped_generate;
            continue;
          }

          const auto& fixture = canonical_fixture;
          ResultSet expected;
          ++cases_attempted;
          std::cerr << "RAICode " << case_id << " (out=" << program.output_def << ")...\n" << std::flush;
          try {
            expected = RelEngine::Run(program.source, fixture, program.output_def, &program.schema);
          } catch (const std::exception& ex) {
            ++skipped_rel;
            if (mismatch_out.is_open()) {
              WriteRelSkipMismatch(mismatch_out, case_id, ex.what(), program.source);
            }
            LogRelSkip(std::cerr, case_id, ex.what(), program.source);
            continue;
          } catch (...) {
            ++skipped_rel;
            if (mismatch_out.is_open()) {
              WriteRelSkipMismatch(mismatch_out, case_id, "unknown error", program.source);
            }
            LogRelSkip(std::cerr, case_id, "unknown error", program.source);
            continue;
          }
          if (IsEmpty(expected)) {
            ++skipped_empty;
            if (mismatch_out.is_open()) {
              mismatch_out << "{\"id\":\"" << case_id << "\",\"reason\":\"empty_oracle\"}\n";
            }
            continue;
          }

          auto sql = GetSQLRel(program.source, config.edb_map);
          if (!sql) {
            ++skipped_mismatch;
            continue;
          }
          try {
            const auto sql_result =
                SqlExecutor::RunProgram(sql->ToString(), fixture, program.output_def, &program.schema);
            if (IsEmpty(sql_result)) {
              ++skipped_empty;
              if (mismatch_out.is_open()) {
                mismatch_out << "{\"id\":\"" << case_id << "\",\"reason\":\"empty_sql\"}\n";
              }
              continue;
            }
            if (sql_result != expected) {
              ++skipped_mismatch;
              if (mismatch_out.is_open()) {
                mismatch_out << "{\"id\":\"" << case_id << "\",\"reason\":\"sql_mismatch\"}\n";
              }
              continue;
            }
          } catch (...) {
            ++skipped_mismatch;
            if (mismatch_out.is_open()) {
              mismatch_out << "{\"id\":\"" << case_id << "\",\"reason\":\"sql_exec_error\"}\n";
            }
            continue;
          }

          if (seen_programs.count(program.source)) {
            ++skipped_duplicate;
            continue;
          }

          CorpusCase corpus_case;
          corpus_case.id = case_id;
          corpus_case.config = config;
          corpus_case.program = program.source;
          corpus_case.output_def = program.output_def;
          corpus_case.schema = program.schema;
          corpus_case.edb = fixture.Rows();
          corpus_case.expected = expected;

          if (shard_line_count >= opts.shard_size) {
            open_next_shard();
          }
          shard_out << SerializeCorpusCaseLine(corpus_case) << "\n";
          ++shard_line_count;
          ++total_cases;
          existing_ids.insert(case_id);
          seen_programs.insert(program.source);

          if (total_cases > 0 && total_cases % 10 == 0) {
            std::cerr << "built " << total_cases << " cases (attempted=" << cases_attempted
                      << ", skipped rel=" << skipped_rel << ", mismatch=" << skipped_mismatch
                      << ", empty=" << skipped_empty << ")\n"
                      << std::flush;
          }
        }
      }
    }
  }

done:
  if (shard_out.is_open()) shard_out.close();

  shard_names.clear();
  for (const auto& entry : std::filesystem::directory_iterator(opts.output_dir)) {
    if (entry.path().extension() == ".jsonl") {
      const auto name = entry.path().filename().string();
      if (name.starts_with("shard_")) {
        shard_names.push_back(name);
      }
    }
  }
  std::sort(shard_names.begin(), shard_names.end());
  total_cases = existing_ids.size();

  if (mismatch_out.is_open()) mismatch_out.close();

  WriteManifest(opts.output_dir, total_cases, shard_names);
  std::cout << "corpus build complete: " << total_cases << " cases in " << shard_names.size() << " shards\n";
  std::cout << "skipped generate: " << skipped_generate << ", skipped rel: " << skipped_rel
            << ", skipped empty: " << skipped_empty << ", skipped mismatch: " << skipped_mismatch
            << ", skipped duplicate: " << skipped_duplicate << "\n";
  return 0;
}
