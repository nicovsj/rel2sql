#include "generator/corpus_case_config.h"

#include <sstream>
#include <stdexcept>

#include "test_common.h"

namespace rel2sql::generator {

GeneratorProfile FullCorpusProfile() {
  GeneratorProfile profile;
  profile.allow_recursion = false;
  profile.allow_forall = true;
  profile.allow_aggregates = true;
  profile.allow_partial_app = true;
  profile.allow_products = true;
  profile.allow_extensional = true;
  profile.allow_arithmetic = true;
  profile.allow_comparisons = true;
  profile.allow_negation = true;
  profile.allow_where = true;
  profile.allow_binding_exprs = true;
  profile.max_defs = 1;
  return profile;
}

GeneratorProfile QuantifiersCorpusProfile() {
  GeneratorProfile profile;
  profile.allow_forall = true;
  profile.allow_comparisons = false;
  profile.allow_negation = false;
  profile.allow_arithmetic = false;
  profile.focus_quantifiers = true;
  profile.max_defs = 1;
  return profile;
}

GeneratorProfile ExprAlgebraCorpusProfile() {
  GeneratorProfile profile;
  profile.allow_partial_app = true;
  profile.allow_products = true;
  profile.allow_extensional = true;
  profile.focus_expr_algebra = true;
  profile.max_defs = 1;
  return profile;
}

GeneratorProfile AggregatesCorpusProfile() {
  GeneratorProfile profile;
  profile.allow_aggregates = true;
  profile.allow_partial_app = true;
  profile.focus_aggregates = true;
  profile.max_defs = 1;
  return profile;
}

std::vector<ProfilePreset> CorpusProfilePresets() {
  return {{"full", FullCorpusProfile()},
          {"quantifiers", QuantifiersCorpusProfile()},
          {"expr_algebra", ExprAlgebraCorpusProfile()},
          {"aggregates", AggregatesCorpusProfile()}};
}

std::vector<uint64_t> CorpusBuildSeeds() { return {1, 7, 42, 99}; }

std::vector<size_t> CorpusBuildProgramIndices() {
  std::vector<size_t> indices;
  indices.reserve(kCorpusBuildProgramIndexCount);
  for (size_t i = 0; i < kCorpusBuildProgramIndexCount; ++i) indices.push_back(i);
  return indices;
}

std::vector<size_t> CorpusBuildBudgets() { return {6, 8, 10, 12, 14, 16}; }

size_t CorpusBuildGridSlotCount() {
  return CorpusProfilePresets().size() * CorpusBuildSeeds().size() * CorpusBuildProgramIndices().size() *
         CorpusBuildBudgets().size();
}

std::optional<GeneratorProfile> ProfileByName(const std::string& name) {
  for (const auto& preset : CorpusProfilePresets()) {
    if (preset.name == name) return preset.profile;
  }
  return std::nullopt;
}

std::optional<ParsedCaseId> ParseCaseId(const std::string& case_id) {
  // s{seed}_i{index}_b{budget}_p{profile}
  if (case_id.empty() || case_id[0] != 's') return std::nullopt;

  ParsedCaseId out;
  try {
    size_t pos = 1;
    const size_t i_pos = case_id.find("_i", pos);
    if (i_pos == std::string::npos) return std::nullopt;
    out.seed = std::stoull(case_id.substr(pos, i_pos - pos));

    pos = i_pos + 2;
    const size_t b_pos = case_id.find("_b", pos);
    if (b_pos == std::string::npos) return std::nullopt;
    out.program_index = static_cast<size_t>(std::stoull(case_id.substr(pos, b_pos - pos)));

    pos = b_pos + 2;
    const size_t p_pos = case_id.find("_p", pos);
    if (p_pos == std::string::npos) return std::nullopt;
    out.node_budget = static_cast<size_t>(std::stoull(case_id.substr(pos, p_pos - pos)));

    out.profile_name = case_id.substr(p_pos + 2);
    if (out.profile_name.empty() || !ProfileByName(out.profile_name)) return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
  return out;
}

SuiteConfig SuiteConfigFromCaseId(const ParsedCaseId& parsed) {
  SuiteConfig config;
  config.seed = parsed.seed;
  config.program_index = parsed.program_index;
  config.node_budget = parsed.node_budget;
  config.edb_map = CreateDefaultEDBMap();
  config.profile = ProfileByName(parsed.profile_name).value();
  return config;
}

std::string MakeCaseId(uint64_t seed, size_t program_index, size_t node_budget, const std::string& profile_name) {
  std::ostringstream os;
  os << "s" << seed << "_i" << program_index << "_b" << node_budget << "_p" << profile_name;
  return os.str();
}

}  // namespace rel2sql::generator
