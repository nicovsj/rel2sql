#ifndef REL2SQL_TESTS_GENERATOR_CORPUS_CASE_CONFIG_H_
#define REL2SQL_TESTS_GENERATOR_CORPUS_CASE_CONFIG_H_

#include <optional>
#include <string>
#include <vector>

#include "generator/rel_program_generator.h"

namespace rel2sql::generator {

// Corpus build grid dimensions (see CorpusBuild* helpers). Bump kCorpusGeneratorFingerprint when changed.
inline constexpr size_t kCorpusBuildProgramIndexCount = 128;

// Corpus v2 programs have up to this many defs, whose dependency graph reaches every def from the output.
inline constexpr int kCorpusV2MaxDefs = 3;

struct ProfilePreset {
  std::string name;
  GeneratorProfile profile;
};

// Corpus v1 presets (single def, the VLDB 2026 artifact).
std::vector<ProfilePreset> CorpusProfilePresets();

// Corpus v2 presets: the v1 presets, named with a `_rules` suffix, with up to kCorpusV2MaxDefs defs. `full_rules`
// also allows linear recursion in some of its defs.
std::vector<ProfilePreset> CorpusV2ProfilePresets();

GeneratorProfile FullCorpusProfile();
GeneratorProfile QuantifiersCorpusProfile();
GeneratorProfile ExprAlgebraCorpusProfile();
GeneratorProfile AggregatesCorpusProfile();

std::vector<uint64_t> CorpusBuildSeeds();

std::vector<size_t> CorpusBuildProgramIndices();

std::vector<size_t> CorpusBuildBudgets();

// presets × seeds × indices × budgets
size_t CorpusBuildGridSlotCount();

std::optional<GeneratorProfile> ProfileByName(const std::string& name);

// Parses ids like s1_i0_b12_pfull, or s1_i0_b12_pfull_rules_v2 for variant 2
struct ParsedCaseId {
  uint64_t seed = 0;
  size_t program_index = 0;
  size_t node_budget = 0;
  std::string profile_name;
  uint64_t variant = 0;
};

std::optional<ParsedCaseId> ParseCaseId(const std::string& case_id);

SuiteConfig SuiteConfigFromCaseId(const ParsedCaseId& parsed);

std::string MakeCaseId(uint64_t seed, size_t program_index, size_t node_budget, const std::string& profile_name,
                       uint64_t variant = 0);

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_CORPUS_CASE_CONFIG_H_
