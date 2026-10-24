#ifndef REL2SQL_TESTS_GENERATOR_CORPUS_CASE_CONFIG_H_
#define REL2SQL_TESTS_GENERATOR_CORPUS_CASE_CONFIG_H_

#include <optional>
#include <string>
#include <vector>

#include "generator/rel_program_generator.h"

namespace rel2sql::generator {

// Corpus build grid dimensions (see CorpusBuild* helpers). Bump kCorpusGeneratorFingerprint when changed.
inline constexpr size_t kCorpusBuildProgramIndexCount = 128;

struct ProfilePreset {
  std::string name;
  GeneratorProfile profile;
};

std::vector<ProfilePreset> CorpusProfilePresets();

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

// Parses ids like s1_i0_b12_pfull
struct ParsedCaseId {
  uint64_t seed = 0;
  size_t program_index = 0;
  size_t node_budget = 0;
  std::string profile_name;
};

std::optional<ParsedCaseId> ParseCaseId(const std::string& case_id);

SuiteConfig SuiteConfigFromCaseId(const ParsedCaseId& parsed);

std::string MakeCaseId(uint64_t seed, size_t program_index, size_t node_budget, const std::string& profile_name);

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_CORPUS_CASE_CONFIG_H_
