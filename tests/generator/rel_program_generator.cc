#include "generator/rel_program_generator.h"

#include <algorithm>
#include <array>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "api/translate.h"
#include "generator/rng.h"

namespace rel2sql::generator {
namespace {

constexpr int kMaxVerifyAttempts = 32;
constexpr std::array<const char*, 8> kVarNames = {"x", "y", "z", "w", "u", "v", "t", "s"};
constexpr std::array<const char*, 4> kAggOps = {"sum", "max", "min", "average"};

struct RelationPick {
  std::string name;
  int arity;
};

bool IsEDBName(const std::string& name, const RelationMap& edb_map) { return edb_map.map.contains(name); }

std::vector<RelationPick> RelationsWithMaxArity(const RelationMap& schema, int max_arity) {
  std::vector<RelationPick> out;
  for (const auto& kv : schema.map) {
    if (kv.second.arity >= 1 && kv.second.arity <= max_arity) {
      out.push_back({kv.first, kv.second.arity});
    }
  }
  return out;
}

std::vector<std::string> EdbUnaryTables(const RelationMap& schema, const RelationMap& edb_map) {
  std::vector<std::string> out;
  for (const auto& kv : schema.map) {
    if (kv.second.arity == 1 && IsEDBName(kv.first, edb_map)) {
      out.push_back(kv.first);
    }
  }
  return out;
}

std::string FormatApp(const std::string& rel, const std::vector<std::string>& args) {
  std::ostringstream os;
  os << rel << "(";
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0) os << ", ";
    os << args[i];
  }
  os << ")";
  return os.str();
}

std::string FormatPartialApp(const std::string& rel, const std::vector<std::string>& args) {
  std::ostringstream os;
  os << rel << "[";
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0) os << ", ";
    os << args[i];
  }
  os << "]";
  return os.str();
}

std::string PickAtom(Rng& rng, const RelationMap& schema, const std::vector<std::string>& bound_vars) {
  auto candidates = RelationsWithMaxArity(schema, static_cast<int>(bound_vars.size()));
  if (candidates.empty()) {
    return FormatApp("A", {bound_vars.front()});
  }
  const auto& pick = candidates[rng.Uniform(candidates.size())];
  std::vector<std::string> args(bound_vars.begin(), bound_vars.begin() + static_cast<size_t>(pick.arity));
  rng.Shuffle(args);
  return FormatApp(pick.name, args);
}

std::optional<std::string> GenFormula(Rng& rng, size_t budget, const std::vector<std::string>& bound_vars,
                                      const RelationMap& schema, const RelationMap& edb_map,
                                      const GeneratorProfile& profile);

std::optional<std::string> GenPartialApp(Rng& rng, const RelationMap& schema,
                                         const std::vector<std::string>& bound_vars) {
  auto candidates = RelationsWithMaxArity(schema, static_cast<int>(bound_vars.size()));
  std::vector<RelationPick> partial_candidates;
  for (const auto& c : candidates) {
    if (c.arity >= 2) partial_candidates.push_back(c);
  }
  if (partial_candidates.empty()) return std::nullopt;

  const auto pick = partial_candidates[rng.Uniform(partial_candidates.size())];
  int supplied = rng.UniformInt(1, pick.arity - 1);
  std::vector<std::string> args(bound_vars.begin(), bound_vars.begin() + static_cast<size_t>(supplied));
  rng.Shuffle(args);
  return FormatPartialApp(pick.name, args);
}

std::optional<std::string> GenAggregate(Rng& rng, const RelationMap& schema,
                                        const std::vector<std::string>& bound_vars) {
  auto candidates = RelationsWithMaxArity(schema, static_cast<int>(bound_vars.size()));
  if (candidates.empty()) return std::nullopt;

  const auto& pick = candidates[rng.Uniform(candidates.size())];
  const char* agg = kAggOps[rng.Uniform(std::size(kAggOps))];
  if (pick.arity == 1) {
    return std::string(agg) + "[" + FormatApp(pick.name, {bound_vars.front()}) + "]";
  }
  std::vector<std::string> args(bound_vars.begin(), bound_vars.begin() + static_cast<size_t>(pick.arity));
  rng.Shuffle(args);
  return std::string(agg) + "[" + FormatApp(pick.name, args) + "]";
}

std::optional<std::string> GenFormula(Rng& rng, size_t budget, const std::vector<std::string>& bound_vars,
                                      const RelationMap& schema, const RelationMap& edb_map,
                                      const GeneratorProfile& profile) {
  if (budget == 0 || bound_vars.empty()) return std::nullopt;
  if (budget == 1) return PickAtom(rng, schema, bound_vars);

  enum class Choice { kAnd, kOr, kExists, kForall, kAtom, kPartial, kAgg };
  std::vector<Choice> choices = {Choice::kAnd, Choice::kAtom};
  if (budget >= 3) choices.push_back(Choice::kExists);
  if (profile.allow_forall && budget >= 4 && !EdbUnaryTables(schema, edb_map).empty()) {
    choices.push_back(Choice::kForall);
  }
  if (budget >= 2 && !bound_vars.empty()) choices.push_back(Choice::kOr);
  if (profile.allow_partial_app && budget >= 2) choices.push_back(Choice::kPartial);
  if (profile.allow_aggregates && budget >= 2) choices.push_back(Choice::kAgg);

  const Choice choice = choices[rng.Uniform(choices.size())];

  switch (choice) {
    case Choice::kAtom:
      return PickAtom(rng, schema, bound_vars);
    case Choice::kPartial: {
      auto partial = GenPartialApp(rng, schema, bound_vars);
      if (partial) return partial;
      return PickAtom(rng, schema, bound_vars);
    }
    case Choice::kAgg: {
      auto agg = GenAggregate(rng, schema, bound_vars);
      if (agg) return agg;
      return PickAtom(rng, schema, bound_vars);
    }
    case Choice::kAnd: {
      size_t left_budget = 1 + rng.Uniform(budget - 1);
      size_t right_budget = budget - 1 - left_budget;
      if (right_budget == 0) {
        right_budget = 1;
        left_budget = budget - 2;
      }
      auto lhs = GenFormula(rng, left_budget, bound_vars, schema, edb_map, profile);
      auto rhs = GenFormula(rng, right_budget, bound_vars, schema, edb_map, profile);
      if (!lhs || !rhs) return PickAtom(rng, schema, bound_vars);
      return "(" + *lhs + " and " + *rhs + ")";
    }
    case Choice::kOr: {
      if (budget < 3) return PickAtom(rng, schema, bound_vars);
      size_t left_budget = 1 + rng.Uniform(budget - 2);
      size_t right_budget = budget - 1 - left_budget;
      if (right_budget == 0) {
        right_budget = 1;
        left_budget = budget - 2;
      }
      auto lhs = GenFormula(rng, left_budget, bound_vars, schema, edb_map, profile);
      auto rhs = GenFormula(rng, right_budget, bound_vars, schema, edb_map, profile);
      if (!lhs || !rhs) return PickAtom(rng, schema, bound_vars);
      return "(" + *lhs + " or " + *rhs + ")";
    }
    case Choice::kExists: {
      if (bound_vars.size() >= kVarNames.size()) return PickAtom(rng, schema, bound_vars);
      const std::string fresh = kVarNames[bound_vars.size()];
      std::vector<std::string> inner_vars = bound_vars;
      inner_vars.push_back(fresh);
      auto body = GenFormula(rng, budget - 1, inner_vars, schema, edb_map, profile);
      if (!body) return PickAtom(rng, schema, bound_vars);
      return "exists((" + fresh + ") | " + *body + ")";
    }
    case Choice::kForall: {
      auto tables = EdbUnaryTables(schema, edb_map);
      if (tables.empty() || bound_vars.size() >= kVarNames.size()) {
        return PickAtom(rng, schema, bound_vars);
      }
      const std::string fresh = kVarNames[bound_vars.size()];
      const std::string table = tables[rng.Uniform(tables.size())];
      std::vector<std::string> inner_vars = bound_vars;
      inner_vars.push_back(fresh);
      auto body = GenFormula(rng, budget - 2, inner_vars, schema, edb_map, profile);
      if (!body) return PickAtom(rng, schema, bound_vars);
      return "forall((" + fresh + " in " + table + ") | " + *body + ")";
    }
  }
  return PickAtom(rng, schema, bound_vars);
}

std::string GenBindingHead(int arity) {
  std::ostringstream os;
  os << "(";
  for (int i = 0; i < arity; ++i) {
    if (i > 0) os << ", ";
    os << kVarNames[static_cast<size_t>(i)];
  }
  os << ")";
  return os.str();
}

std::vector<std::string> BindingVars(int arity) {
  std::vector<std::string> vars;
  vars.reserve(static_cast<size_t>(arity));
  for (int i = 0; i < arity; ++i) {
    vars.push_back(kVarNames[static_cast<size_t>(i)]);
  }
  return vars;
}

std::string GenIntensionalBody(Rng& rng, size_t budget, int arity, const RelationMap& schema,
                               const RelationMap& edb_map, const GeneratorProfile& profile) {
  const auto bound_vars = BindingVars(arity);
  auto formula = GenFormula(rng, budget, bound_vars, schema, edb_map, profile);
  if (!formula) formula = PickAtom(rng, schema, bound_vars);
  return GenBindingHead(arity) + ": " + *formula;
}

std::string GenExtensionalBody(Rng& rng, size_t budget, int arity) {
  std::ostringstream os;
  const int tuple_count = std::max(1, static_cast<int>(budget));
  for (int i = 0; i < tuple_count; ++i) {
    if (i > 0) os << "; ";
    os << "(";
    for (int j = 0; j < arity; ++j) {
      if (j > 0) os << ", ";
      const int value = rng.UniformInt(1, 9);
      os << value;
    }
    os << ")";
  }
  return os.str();
}

std::string GenProductBody(Rng& rng, size_t /*budget*/, const RelationMap& schema) {
  auto unary = RelationsWithMaxArity(schema, 1);
  if (unary.size() < 2) {
    return "(x, y): A(x) and B(y)";
  }
  const auto& left = unary[rng.Uniform(unary.size())];
  const auto& right = unary[rng.Uniform(unary.size())];
  return "(x, y): " + FormatApp(left.name, {"x"}) + " and " + FormatApp(right.name, {"y"});
}

std::optional<std::string> PickBinaryEdb(const RelationMap& edb_map) {
  std::vector<std::string> names;
  for (const auto& kv : edb_map.map) {
    if (kv.second.arity == 2) names.push_back(kv.first);
  }
  if (names.empty()) return std::nullopt;
  return names.front();
}

std::string GenRecursiveBody(const std::string& self_name, const std::string& base_rel) {
  return "(x, y): " + base_rel + "(x, y) or exists((z) | " + base_rel + "(x, z) and " + self_name + "(z, y))";
}

std::string MakeIdbName(int index) { return "Gen" + std::to_string(index); }

GeneratedProgram TryGenerateOnce(const SuiteConfig& config, uint64_t attempt) {
  Rng rng(MixSeed(config.seed, config.program_index, attempt));

  RelationMap schema = config.edb_map;
  const size_t max_defs_by_budget = std::max<size_t>(1, config.node_budget / 2);
  const size_t max_defs = std::min(static_cast<size_t>(std::max(1, config.profile.max_defs)), max_defs_by_budget);
  const int num_defs = static_cast<int>(1 + rng.Uniform(max_defs));

  std::vector<size_t> body_budgets(static_cast<size_t>(num_defs), 1);
  const size_t overhead = static_cast<size_t>(num_defs);
  size_t formula_budget = config.node_budget > overhead ? config.node_budget - overhead : 1;
  formula_budget = std::max(formula_budget, static_cast<size_t>(num_defs));
  size_t extra = formula_budget - static_cast<size_t>(num_defs);
  for (size_t i = 0; i < extra; ++i) {
    body_budgets[rng.Uniform(static_cast<size_t>(num_defs))]++;
  }

  std::ostringstream program;
  std::vector<std::string> def_names;
  def_names.reserve(static_cast<size_t>(num_defs));

  for (int i = 0; i < num_defs; ++i) {
    const std::string name = MakeIdbName(i);
    def_names.push_back(name);

    const bool is_last = i == num_defs - 1;

    std::string body;
    int arity = 1;
    bool did_recursion = false;

    if (is_last && config.profile.allow_recursion && body_budgets[static_cast<size_t>(i)] >= 4) {
      if (auto base_rel = PickBinaryEdb(config.edb_map)) {
        arity = 2;
        body = GenRecursiveBody(name, *base_rel);
        did_recursion = true;
      }
    }

    if (!did_recursion) {
      const bool use_extensional =
          config.profile.allow_extensional && rng.CoinFlip() && body_budgets[static_cast<size_t>(i)] >= 2;
      const bool use_product = config.profile.allow_products && !use_extensional && rng.CoinFlip() &&
                               body_budgets[static_cast<size_t>(i)] >= 3;

      if (use_extensional) {
        arity = rng.UniformInt(1, 3);
        body = GenExtensionalBody(rng, body_budgets[static_cast<size_t>(i)], arity);
      } else if (use_product) {
        arity = 2;
        body = GenProductBody(rng, body_budgets[static_cast<size_t>(i)], schema);
      } else {
        arity = rng.UniformInt(1, 3);
        body = GenIntensionalBody(rng, body_budgets[static_cast<size_t>(i)], arity, schema, config.edb_map,
                                  config.profile);
      }
    }

    if (i > 0) program << "\n";
    program << "def " << name << " {" << body << "}";
    schema[name] = RelationInfo(arity);
  }

  GeneratedProgram result;
  result.source = program.str();
  result.schema = schema;
  result.output_def = def_names.back();
  return result;
}

}  // namespace

bool VerifyProgram(const std::string& source, const RelationMap& edb_map) {
  try {
    auto sql = GetSQLRel(source, edb_map);
    return sql != nullptr;
  } catch (...) {
    return false;
  }
}

GeneratedProgram GenerateProgram(const SuiteConfig& config) {
  if (config.node_budget < 2) {
    throw GenerationException("node_budget must be at least 2");
  }
  if (config.edb_map.map.empty()) {
    throw GenerationException("edb_map must not be empty");
  }

  for (uint64_t attempt = 0; attempt < kMaxVerifyAttempts; ++attempt) {
    auto candidate = TryGenerateOnce(config, attempt);
    if (VerifyProgram(candidate.source, config.edb_map)) {
      return candidate;
    }
  }

  throw GenerationException("failed to generate a verifiable program after " + std::to_string(kMaxVerifyAttempts) +
                            " attempts");
}

}  // namespace rel2sql::generator
