// Builds the golden Rel-oracle corpus via the persistent Rel engine server.
// Usage: build_corpus [--version 1|2] [--output-dir PATH] [--shard-size N] [--resume] [--fresh] [--max-cases N]
//                     [--profiles a,b] [--seeds 1,7] [--indices N] [--budgets 6,8] [--nonempty-attempts N]
//
// Every generated program gets one line in outcomes.jsonl, with one of the outcomes below. Programs on which the
// translation and the Rel engine agree go to the shards, including those whose answer is empty (flagged by
// "rel_nonempty"). A slot is retried with further variants of its program, up to --nonempty-attempts programs,
// until the Rel engine gives a non-empty answer.
//
//   generation_failed     no well-formed program for this slot and variant
//   duplicate             same program text as an earlier case (not evaluated again)
//   outside_fragment      integer and float values may meet (see ProgramMixesIntegersAndFloats); not evaluated
//   translation_rejected  the translation rejects the program (the Rel result is still recorded)
//   rel_error             the Rel engine fails on the program
//   sql_exec_error        DuckDB fails on the translated SQL
//   mismatch              both run, with different answers
//   match                 both run, with the same answer

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
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
  int version = 2;
  std::optional<std::filesystem::path> output_dir;
  size_t shard_size = 150;
  bool resume = false;
  bool fresh = false;
  std::optional<size_t> max_cases;
  std::optional<std::vector<std::string>> profiles;
  std::optional<std::vector<uint64_t>> seeds;
  std::optional<size_t> index_count;
  std::optional<std::vector<size_t>> budgets;
  uint64_t nonempty_attempts = 3;
};

std::vector<std::string> SplitComma(const std::string& text) {
  std::vector<std::string> parts;
  std::istringstream in(text);
  std::string part;
  while (std::getline(in, part, ',')) {
    if (!part.empty()) parts.push_back(part);
  }
  return parts;
}

BuildOptions ParseArgs(int argc, char** argv) {
  BuildOptions opts;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--resume") {
      opts.resume = true;
    } else if (arg == "--fresh") {
      opts.fresh = true;
    } else if (arg == "--version" && has_value) {
      opts.version = std::stoi(argv[++i]);
    } else if (arg == "--output-dir" && has_value) {
      opts.output_dir = argv[++i];
    } else if (arg == "--shard-size" && has_value) {
      opts.shard_size = static_cast<size_t>(std::stoull(argv[++i]));
    } else if (arg == "--max-cases" && has_value) {
      opts.max_cases = static_cast<size_t>(std::stoull(argv[++i]));
    } else if (arg == "--profiles" && has_value) {
      opts.profiles = SplitComma(argv[++i]);
    } else if (arg == "--seeds" && has_value) {
      std::vector<uint64_t> seeds;
      for (const auto& s : SplitComma(argv[++i])) seeds.push_back(std::stoull(s));
      opts.seeds = seeds;
    } else if (arg == "--indices" && has_value) {
      opts.index_count = static_cast<size_t>(std::stoull(argv[++i]));
    } else if (arg == "--budgets" && has_value) {
      std::vector<size_t> budgets;
      for (const auto& b : SplitComma(argv[++i])) budgets.push_back(static_cast<size_t>(std::stoull(b)));
      opts.budgets = budgets;
    } else if (arg == "--nonempty-attempts" && has_value) {
      opts.nonempty_attempts = std::max<uint64_t>(1, std::stoull(argv[++i]));
    }
  }
  return opts;
}

std::string TruncateForLog(std::string_view text, size_t max_len = 500) {
  if (text.size() <= max_len) return std::string(text);
  return std::string(text.substr(0, max_len)) + "...";
}

std::string OneLine(std::string_view text) {
  std::string out(text);
  std::replace(out.begin(), out.end(), '\n', ' ');
  return out;
}

// One line of outcomes.jsonl. The program comes last, so that resuming can read it back without a JSON parser.
struct Outcome {
  std::string id;
  std::string outcome;
  std::string rel;  // "nonempty", "empty", "error", or "" when the Rel engine was not run
  std::string error;
  std::string program;
};

std::string SerializeOutcome(const Outcome& outcome) {
  std::ostringstream os;
  os << "{\"id\":\"" << JsonEscape(outcome.id) << "\",\"outcome\":\"" << outcome.outcome << "\"";
  if (!outcome.rel.empty()) os << ",\"rel\":\"" << outcome.rel << "\"";
  os << ",\"rel_nonempty\":" << (outcome.rel == "nonempty" ? "true" : "false");
  if (!outcome.error.empty()) os << ",\"error\":\"" << JsonEscape(TruncateForLog(OneLine(outcome.error))) << "\"";
  os << ",\"program\":\"" << JsonEscape(outcome.program) << "\"}";
  return os.str();
}

std::optional<std::string> StringField(const std::string& line, const std::string& key) {
  const std::string marker = "\"" + key + "\":\"";
  const size_t start = line.find(marker);
  if (start == std::string::npos) return std::nullopt;
  const size_t value = start + marker.size();
  const size_t end = line.find('"', value);
  if (end == std::string::npos) return std::nullopt;
  return line.substr(value, end - value);
}

// Escaped program text of an outcome line (the last field).
std::optional<std::string> EscapedProgramField(const std::string& line) {
  const std::string marker = "\"program\":\"";
  const size_t start = line.rfind(marker);
  if (start == std::string::npos || line.size() < start + marker.size() + 2) return std::nullopt;
  const size_t value = start + marker.size();
  return line.substr(value, line.size() - value - 2);
}

struct OutcomeLog {
  std::set<std::string> ids;
  std::set<std::string> nonempty_slots;  // ids without the variant suffix
  std::set<std::string> escaped_programs;
  std::map<std::string, size_t> counts;
  size_t nonempty_matches = 0;
};

std::string SlotId(const std::string& case_id) {
  const size_t v_pos = case_id.rfind("_v");
  if (v_pos == std::string::npos) return case_id;
  const bool digits =
      v_pos + 2 < case_id.size() && std::all_of(case_id.begin() + static_cast<std::ptrdiff_t>(v_pos + 2), case_id.end(),
                                                [](unsigned char c) { return std::isdigit(c); });
  return digits ? case_id.substr(0, v_pos) : case_id;
}

OutcomeLog LoadOutcomes(const std::filesystem::path& path) {
  OutcomeLog log;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    const auto id = StringField(line, "id");
    const auto outcome = StringField(line, "outcome");
    if (!id || !outcome) continue;
    log.ids.insert(*id);
    ++log.counts[*outcome];
    const bool rel_nonempty = line.find("\"rel_nonempty\":true") != std::string::npos;
    if (rel_nonempty) log.nonempty_slots.insert(SlotId(*id));
    if (*outcome == "match" && rel_nonempty) ++log.nonempty_matches;
    if (*outcome != "duplicate" && *outcome != "generation_failed") {
      if (auto program = EscapedProgramField(line)) log.escaped_programs.insert(*program);
    }
  }
  return log;
}

size_t CountShards(const std::filesystem::path& output_dir) {
  size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(output_dir)) {
    const auto name = entry.path().filename().string();
    if (entry.is_regular_file() && name.starts_with("shard_") && name.ends_with(".jsonl")) ++count;
  }
  return count;
}

void WriteManifest(const std::filesystem::path& output_dir, int version, const OutcomeLog& log) {
  const auto count = [&](const std::string& key) {
    const auto it = log.counts.find(key);
    return it == log.counts.end() ? size_t{0} : it->second;
  };
  size_t total = 0;
  for (const auto& kv : log.counts) total += kv.second;

  std::ostringstream os;
  os << "{\n";
  os << "  \"corpus_version\": " << version << ",\n";
  os << "  \"generator_fingerprint\": \""
     << (version == 1 ? kCorpusGeneratorFingerprint : kCorpusV2GeneratorFingerprint) << "\",\n";
  os << "  \"edb_map\": \"default\",\n";
  os << "  \"case_count\": " << count("match") << ",\n";
  os << "  \"nonempty_case_count\": " << log.nonempty_matches << ",\n";
  os << "  \"shard_count\": " << CountShards(output_dir) << ",\n";
  os << "  \"programs\": " << total << ",\n";
  os << "  \"outcomes\": {";
  bool first = true;
  for (const auto& kv : log.counts) {
    os << (first ? "" : ",") << "\n    \"" << kv.first << "\": " << kv.second;
    first = false;
  }
  os << "\n  }\n";
  os << "}\n";
  std::ofstream out(output_dir / "manifest.json");
  out << os.str();
}

}  // namespace
}  // namespace rel2sql::generator

int main(int argc, char** argv) {
  using namespace rel2sql::generator;

  const auto opts = ParseArgs(argc, argv);
  if (opts.version != 1 && opts.version != 2) {
    std::cerr << "--version must be 1 or 2\n";
    return 1;
  }
  if (!RelEngine::IsAvailable()) {
    std::cerr << "RAICode not available. Start rel-engine server and set REL2SQL_ENABLE_RAICODE=1\n";
    return 1;
  }
  if (!RelEngine::IsServerReachable()) {
    std::cerr << "rel engine server not reachable — start with: task rel-engine:start\n";
    return 1;
  }

  const auto output_dir = opts.output_dir.value_or(CorpusRoot(opts.version));
  std::filesystem::create_directories(output_dir);
  const auto outcomes_path = output_dir / "outcomes.jsonl";

  const bool has_outcomes = std::filesystem::exists(outcomes_path);
  if (has_outcomes && !opts.resume && !opts.fresh) {
    std::cerr << "corpus outcomes already exist in " << output_dir
              << "; pass --resume to add new programs or --fresh to rebuild from scratch\n";
    return 1;
  }
  if (opts.fresh) {
    for (const auto& entry : std::filesystem::directory_iterator(output_dir)) {
      if (entry.path().extension() == ".jsonl") std::filesystem::remove(entry.path());
    }
  }

  std::vector<ProfilePreset> presets = opts.version == 1 ? CorpusProfilePresets() : CorpusV2ProfilePresets();
  if (opts.profiles) {
    std::erase_if(presets, [&](const ProfilePreset& preset) {
      return std::find(opts.profiles->begin(), opts.profiles->end(), preset.name) == opts.profiles->end();
    });
  }
  const auto seeds = opts.seeds.value_or(CorpusBuildSeeds());
  auto program_indices = CorpusBuildProgramIndices();
  if (opts.index_count && *opts.index_count < program_indices.size()) program_indices.resize(*opts.index_count);
  const auto budgets = opts.budgets.value_or(CorpusBuildBudgets());
  const uint64_t attempts = opts.version == 1 ? 1 : opts.nonempty_attempts;
  const auto canonical_fixture = DataFixture::CreateCanonical(rel2sql::CreateDefaultEDBMap());

  OutcomeLog log = opts.resume ? LoadOutcomes(outcomes_path) : OutcomeLog{};
  std::cerr << "corpus v" << opts.version << " build: " << presets.size() << " profiles x " << seeds.size()
            << " seeds x " << program_indices.size() << " indices x " << budgets.size() << " budgets, up to "
            << attempts << " programs per slot";
  if (opts.resume) std::cerr << " (resuming after " << log.ids.size() << " programs)";
  std::cerr << "\nfirst RAICode query often takes 2-3 minutes; watch progress in .rel_engine.log\n" << std::flush;

  std::ofstream outcomes_out(outcomes_path, std::ios::app);
  std::ofstream mismatch_out(output_dir / "mismatches.jsonl", std::ios::app);
  size_t shard_index = CountShards(output_dir);
  size_t shard_line_count = 0;
  std::ofstream shard_out;
  auto write_case = [&](const CorpusCase& corpus_case) {
    if (!shard_out.is_open() || shard_line_count >= opts.shard_size) {
      if (shard_out.is_open()) shard_out.close();
      std::ostringstream name;
      name << "shard_" << std::setw(3) << std::setfill('0') << shard_index++ << ".jsonl";
      shard_out.open(output_dir / name.str());
      shard_line_count = 0;
    }
    shard_out << SerializeCorpusCaseLine(corpus_case) << "\n" << std::flush;
    ++shard_line_count;
  };

  size_t evaluated = 0;
  auto record = [&](const Outcome& outcome) {
    outcomes_out << SerializeOutcome(outcome) << "\n" << std::flush;
    log.ids.insert(outcome.id);
    ++log.counts[outcome.outcome];
    if (outcome.rel == "nonempty") log.nonempty_slots.insert(SlotId(outcome.id));
    if (outcome.outcome == "match" && outcome.rel == "nonempty") ++log.nonempty_matches;
    if (outcome.outcome == "mismatch" || outcome.outcome == "sql_exec_error") {
      mismatch_out << "{\"id\":\"" << JsonEscape(outcome.id) << "\",\"reason\":\""
                   << (outcome.outcome == "mismatch" ? "sql_mismatch" : "sql_exec_error") << "\"}\n"
                   << std::flush;
    }
  };

  for (const auto& preset : presets) {
    for (uint64_t seed : seeds) {
      for (size_t index : program_indices) {
        for (size_t budget : budgets) {
          const std::string slot_id = MakeCaseId(seed, index, budget, preset.name);
          for (uint64_t variant = 0; variant < attempts; ++variant) {
            if (log.nonempty_slots.count(slot_id)) break;
            if (opts.max_cases && evaluated >= *opts.max_cases) goto done;

            const std::string case_id = MakeCaseId(seed, index, budget, preset.name, variant);
            if (log.ids.count(case_id)) continue;

            SuiteConfig config;
            config.seed = seed;
            config.program_index = index;
            config.node_budget = budget;
            config.edb_map = rel2sql::CreateDefaultEDBMap();
            config.profile = preset.profile;
            config.variant = variant;

            Outcome outcome;
            outcome.id = case_id;

            GeneratedProgram program;
            try {
              program = GenerateProgram(config, /*require_translation=*/false);
            } catch (const std::exception& ex) {
              outcome.outcome = "generation_failed";
              outcome.error = ex.what();
              record(outcome);
              continue;
            }
            outcome.program = program.source;

            const std::string escaped = JsonEscape(program.source);
            if (log.escaped_programs.count(escaped)) {
              outcome.outcome = "duplicate";
              record(outcome);
              continue;
            }
            log.escaped_programs.insert(escaped);
            if (ProgramMixesIntegersAndFloats(program.source)) {
              outcome.outcome = "outside_fragment";
              record(outcome);
              continue;
            }
            ++evaluated;

            std::shared_ptr<rel2sql::sql::ast::Expression> sql;
            std::string translation_error;
            try {
              sql = rel2sql::GetSQLRel(program.source, config.edb_map);
              if (!sql) translation_error = "translation returned no SQL";
            } catch (const std::exception& ex) {
              translation_error = ex.what();
            } catch (...) {
              translation_error = "unknown translation error";
            }

            const auto& fixture = canonical_fixture;
            std::optional<ResultSet> expected;
            std::cerr << "RAICode " << case_id << " (out=" << program.output_def << ")...\n" << std::flush;
            try {
              expected = RelEngine::Run(program.source, fixture, program.output_def, &program.schema);
              outcome.rel = IsEmpty(*expected) ? "empty" : "nonempty";
            } catch (const std::exception& ex) {
              outcome.rel = "error";
              outcome.error = ex.what();
            } catch (...) {
              outcome.rel = "error";
              outcome.error = "unknown Rel engine error";
            }

            if (!translation_error.empty()) {
              outcome.outcome = "translation_rejected";
              if (outcome.error.empty()) outcome.error = translation_error;
              record(outcome);
              continue;
            }
            if (!expected) {
              outcome.outcome = "rel_error";
              record(outcome);
              continue;
            }

            try {
              const auto sql_result =
                  SqlExecutor::RunProgram(sql->ToString(), fixture, program.output_def, &program.schema);
              // The Rel runner infers columns from the rows, so an empty Rel answer has no columns: compare rows only.
              const bool same = IsEmpty(*expected) ? IsEmpty(sql_result) : sql_result == *expected;
              outcome.outcome = same ? "match" : "mismatch";
            } catch (const std::exception& ex) {
              outcome.outcome = "sql_exec_error";
              outcome.error = ex.what();
            } catch (...) {
              outcome.outcome = "sql_exec_error";
              outcome.error = "unknown DuckDB error";
            }

            if (outcome.outcome == "match") {
              CorpusCase corpus_case;
              corpus_case.id = case_id;
              corpus_case.config = config;
              corpus_case.program = program.source;
              corpus_case.output_def = program.output_def;
              corpus_case.schema = program.schema;
              corpus_case.edb = fixture.Rows();
              corpus_case.expected = *expected;
              write_case(corpus_case);
            }
            record(outcome);

            if (evaluated % 10 == 0) {
              std::cerr << "evaluated " << evaluated << " programs this run; matches=" << log.counts["match"]
                        << " (non-empty " << log.nonempty_matches << "), mismatches=" << log.counts["mismatch"]
                        << ", rejected=" << log.counts["translation_rejected"] << "\n"
                        << std::flush;
            }
          }
        }
      }
    }
  }

done:
  if (shard_out.is_open()) shard_out.close();
  outcomes_out.close();
  mismatch_out.close();

  const OutcomeLog totals = LoadOutcomes(outcomes_path);
  WriteManifest(output_dir, opts.version, totals);
  std::cout << "corpus v" << opts.version << " in " << output_dir << ":\n";
  for (const auto& kv : totals.counts) std::cout << "  " << kv.first << ": " << kv.second << "\n";
  std::cout << "  non-empty matches: " << totals.nonempty_matches << "\n";
  return 0;
}
