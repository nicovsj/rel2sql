#include "generator/rel_program_generator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "api/translate.h"
#include "generator/rng.h"
#include "preprocessing/arity_visitor.h"
#include "preprocessing/ids_visitor.h"
#include "rel_ast/rel_ast_builder.h"
#include "rel_ast/rel_ast_visitor.h"
#include "rel_ast/rel_context_builder.h"
#include "sql/aggregate_map.h"

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

std::vector<RelationPick> CallableRelationsWithMaxArity(const RelationMap& schema, int max_arity,
                                                        const std::unordered_set<std::string>& callable) {
  std::vector<RelationPick> out;
  for (const auto& kv : schema.map) {
    if (!callable.contains(kv.first)) continue;
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

constexpr std::array<const char*, 6> kComparators = {"<", "<=", ">", ">=", "=", "!="};

struct DefBodyResult {
  std::string body;
  int arity = 1;
};

std::string GenTerm(Rng& rng, const std::vector<std::string>& bound_vars, bool allow_arithmetic) {
  if (bound_vars.empty()) return std::to_string(rng.UniformInt(1, 5));
  const std::string& var = bound_vars[rng.Uniform(bound_vars.size())];
  if (!allow_arithmetic || rng.Uniform(3) == 0) return var;
  const int constant = rng.UniformInt(1, 5);
  switch (rng.Uniform(3)) {
    case 0:
      return var + " + " + std::to_string(constant);
    case 1:
      return var + " - " + std::to_string(constant);
    default:
      return std::to_string(constant) + " * " + var;
  }
}

bool IsSameVarDifference(const std::string& lhs_expr) {
  const size_t pos = lhs_expr.find(" - ");
  if (pos == std::string::npos) return false;
  const std::string left = lhs_expr.substr(0, pos);
  const std::string right = lhs_expr.substr(pos + 3);
  return !left.empty() && left == right;
}

bool IsStaticallyFalseComparison(const std::string& lhs_expr, const std::string& op, int rhs) {
  if (!IsSameVarDifference(lhs_expr)) return false;
  constexpr int lhs_val = 0;
  if (op == "=") return lhs_val != rhs;
  if (op == "!=") return lhs_val == rhs;
  if (op == "<") return lhs_val >= rhs;
  if (op == "<=") return lhs_val > rhs;
  if (op == ">") return lhs_val <= rhs;
  if (op == ">=") return lhs_val < rhs;
  return false;
}

std::string PickRhsVarForBinop(Rng& rng, const std::vector<std::string>& bound_vars, const std::string& lhs_var,
                               char binop) {
  if (binop == '-') {
    std::vector<std::string> others;
    others.reserve(bound_vars.size());
    for (const auto& v : bound_vars) {
      if (v != lhs_var) others.push_back(v);
    }
    if (others.empty()) return lhs_var;
    return others[rng.Uniform(others.size())];
  }
  return bound_vars[rng.Uniform(bound_vars.size())];
}

std::string GenComparison(Rng& rng, const std::vector<std::string>& bound_vars, const GeneratorProfile& profile) {
  constexpr int kMaxAttempts = 16;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    const char* op = kComparators[rng.Uniform(kComparators.size())];
    const int rhs = rng.UniformInt(1, 5);
    std::string lhs;
    if (profile.allow_arithmetic && bound_vars.size() >= 2 && rng.CoinFlip()) {
      const std::string& lhs_var = bound_vars[rng.Uniform(bound_vars.size())];
      const char* binop = rng.CoinFlip() ? "+" : "-";
      const std::string rhs_var = PickRhsVarForBinop(rng, bound_vars, lhs_var, binop[0]);
      lhs = lhs_var + " " + binop + " " + rhs_var;
    } else {
      lhs = GenTerm(rng, bound_vars, profile.allow_arithmetic);
    }
    if (!IsStaticallyFalseComparison(lhs, op, rhs)) {
      return lhs + " " + op + " " + std::to_string(rhs);
    }
  }

  const std::string& var = bound_vars[rng.Uniform(bound_vars.size())];
  return var + " = " + std::to_string(rng.UniformInt(1, 5));
}

bool ContainsStaticallyFalseComparison(const std::string& source) {
  for (const char* var : kVarNames) {
    const std::string diff = std::string(var) + " - " + var;
    size_t pos = 0;
    while ((pos = source.find(diff, pos)) != std::string::npos) {
      size_t i = pos + diff.size();
      while (i < source.size() && std::isspace(static_cast<unsigned char>(source[i]))) {
        ++i;
      }
      for (const char* op : kComparators) {
        const std::string op_str(op);
        if (i + op_str.size() > source.size() || source.compare(i, op_str.size(), op_str) != 0) {
          continue;
        }
        i += op_str.size();
        while (i < source.size() && std::isspace(static_cast<unsigned char>(source[i]))) {
          ++i;
        }
        int sign = 1;
        if (i < source.size() && source[i] == '-') {
          sign = -1;
          ++i;
        }
        if (i >= source.size() || !std::isdigit(static_cast<unsigned char>(source[i]))) {
          continue;
        }
        int val = 0;
        while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) {
          val = val * 10 + (source[i] - '0');
          ++i;
        }
        val *= sign;
        if (IsStaticallyFalseComparison(diff, op_str, val)) return true;
        break;
      }
      ++pos;
    }
  }
  return false;
}

std::string PickAtom(Rng& rng, const RelationMap& schema, const std::vector<std::string>& bound_vars,
                     const GeneratorProfile& profile, const std::unordered_set<std::string>& callable,
                     bool /*plain_atoms*/ = false) {
  (void)profile;
  auto candidates = CallableRelationsWithMaxArity(schema, static_cast<int>(bound_vars.size()), callable);
  if (candidates.empty()) {
    return FormatApp("A", {bound_vars.empty() ? std::string("x") : bound_vars[0]});
  }
  const auto& pick = candidates[rng.Uniform(candidates.size())];
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(pick.arity));
  const size_t vars_to_cover = std::min(bound_vars.size(), static_cast<size_t>(pick.arity));
  for (size_t i = 0; i < vars_to_cover; ++i) {
    args.push_back(bound_vars[i]);
  }
  while (args.size() < static_cast<size_t>(pick.arity)) {
    if (!bound_vars.empty()) {
      args.push_back(bound_vars[rng.Uniform(bound_vars.size())]);
    } else {
      args.push_back(std::to_string(rng.UniformInt(1, 5)));
    }
  }
  rng.Shuffle(args);
  return FormatApp(pick.name, args);
}

// Atom over a relation whose arity matches |bound_vars|, using each variable once (no arithmetic).
std::string PickAtomGroundingAll(Rng& rng, const RelationMap& schema, const std::vector<std::string>& bound_vars,
                                 const std::unordered_set<std::string>& callable) {
  const int arity = static_cast<int>(bound_vars.size());
  if (arity == 0) return "A()";
  auto candidates = CallableRelationsWithMaxArity(schema, arity, callable);
  std::vector<RelationPick> exact;
  for (const auto& candidate : candidates) {
    if (candidate.arity == arity) exact.push_back(candidate);
  }
  std::vector<std::string> args = bound_vars;
  rng.Shuffle(args);
  if (exact.empty()) {
    return FormatApp("A", args);
  }
  return FormatApp(exact[rng.Uniform(exact.size())].name, args);
}

// Builds an atom that includes required_var among its arguments.
std::string PickAtomIncluding(Rng& rng, const RelationMap& schema, const std::string& required_var,
                              const std::vector<std::string>& pool_vars, const GeneratorProfile& profile,
                              const std::unordered_set<std::string>& callable, bool /*plain_atoms*/ = false) {
  (void)profile;
  const int max_arity = static_cast<int>(pool_vars.size()) + 1;
  auto candidates = CallableRelationsWithMaxArity(schema, std::max(1, max_arity), callable);
  if (candidates.empty()) {
    return FormatApp("A", {required_var});
  }
  const auto& pick = candidates[rng.Uniform(candidates.size())];
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(pick.arity));
  args.push_back(required_var);
  for (int i = 1; i < pick.arity; ++i) {
    if (!pool_vars.empty()) {
      args.push_back(pool_vars[rng.Uniform(pool_vars.size())]);
    } else {
      args.push_back(required_var);
    }
  }
  rng.Shuffle(args);
  return FormatApp(pick.name, args);
}

bool IdentifierAppearsIn(const std::string& text, const std::string& id) {
  if (id.empty()) return false;
  for (size_t pos = 0; pos + id.size() <= text.size(); ++pos) {
    if (text.compare(pos, id.size(), id) != 0) continue;
    const bool left_ok = pos == 0 || !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
    const bool right_ok =
        pos + id.size() >= text.size() ||
        !(std::isalnum(static_cast<unsigned char>(text[pos + id.size()])) || text[pos + id.size()] == '_');
    if (left_ok && right_ok) return true;
  }
  return false;
}

std::optional<std::string> ExtractDefBody(const std::string& source, const std::string& def_name) {
  const std::string marker = std::string("def ") + def_name + " {";
  const size_t pos = source.find(marker);
  if (pos == std::string::npos) return std::nullopt;
  size_t i = pos + marker.size();
  const size_t start = i;
  size_t depth = 1;
  while (i < source.size() && depth > 0) {
    const char c = source[i++];
    if (c == '{')
      ++depth;
    else if (c == '}')
      --depth;
  }
  if (depth != 0) return std::nullopt;
  return source.substr(start, i - start - 1);
}

std::vector<std::string> OrderedProgramDefNames(const std::string& source) {
  std::vector<std::string> names;
  for (size_t pos = 0; pos < source.size();) {
    if (source.compare(pos, 4, "def ") != 0) {
      ++pos;
      continue;
    }
    pos += 4;
    const size_t end = source.find_first_of(" {", pos);
    if (end == std::string::npos) break;
    names.push_back(source.substr(pos, end - pos));
    pos = end;
  }
  return names;
}

std::optional<std::string> OutputDefNameFromSource(const std::string& source) {
  const auto names = OrderedProgramDefNames(source);
  if (names.empty()) return std::nullopt;
  return names.back();
}

std::vector<std::string> IntermediateDefNames(const std::string& source) {
  const auto names = OrderedProgramDefNames(source);
  if (names.size() <= 1) return {};
  return std::vector<std::string>(names.begin(), names.end() - 1);
}

std::string GenAtomForRelation(Rng& rng, const std::string& rel_name, int rel_arity,
                               const std::vector<std::string>& bound_vars) {
  if (rel_arity <= 0) return FormatApp(rel_name, {});
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(rel_arity));
  const size_t vars_to_cover = std::min(bound_vars.size(), static_cast<size_t>(rel_arity));
  for (size_t i = 0; i < vars_to_cover; ++i) {
    args.push_back(bound_vars[i]);
  }
  while (args.size() < static_cast<size_t>(rel_arity)) {
    const size_t arg_idx = args.size();
    if (arg_idx < bound_vars.size()) {
      args.push_back(bound_vars[arg_idx]);
    } else if (arg_idx < std::size(kVarNames)) {
      args.push_back(kVarNames[arg_idx]);
    } else {
      args.push_back("v" + std::to_string(arg_idx));
    }
  }
  rng.Shuffle(args);
  return FormatApp(rel_name, args);
}

std::vector<std::string> GenIdRefsInBody(const std::string& body) {
  std::vector<std::string> refs;
  for (size_t pos = 0; pos + 3 < body.size(); ++pos) {
    if (body.compare(pos, 3, "Gen") != 0) continue;
    size_t end = pos + 3;
    while (end < body.size() && std::isdigit(static_cast<unsigned char>(body[end]))) ++end;
    if (end == pos + 3) continue;
    const std::string name = body.substr(pos, end - pos);
    const bool left_ok = pos == 0 || !(std::isalnum(static_cast<unsigned char>(body[pos - 1])) || body[pos - 1] == '_');
    const bool right_ok =
        end >= body.size() || !(std::isalnum(static_cast<unsigned char>(body[end])) || body[end] == '_');
    if (left_ok && right_ok) {
      refs.push_back(name);
    }
    pos = end - 1;
  }
  return refs;
}

struct QuantifierBodySlice {
  std::string text;
  size_t end_pos = 0;
};

std::optional<QuantifierBodySlice> ExtractQuantifierBody(const std::string& source, size_t pipe_pos) {
  size_t i = pipe_pos + 1;
  while (i < source.size() && std::isspace(static_cast<unsigned char>(source[i]))) ++i;
  if (i >= source.size()) return std::nullopt;

  const size_t start = i;
  int depth = 0;
  for (; i < source.size(); ++i) {
    const char c = source[i];
    if (c == '(') {
      ++depth;
    } else if (c == ')') {
      if (depth == 0) break;
      --depth;
      if (depth == 0) break;
    }
  }
  if (i <= start) return std::nullopt;
  return QuantifierBodySlice{source.substr(start, i - start), i};
}

std::vector<std::string> SplitBindingVars(const std::string& binding) {
  std::vector<std::string> vars;
  std::string current;
  for (char c : binding) {
    if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
      if (!current.empty()) {
        vars.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(c);
  }
  if (!current.empty()) vars.push_back(current);
  return vars;
}

bool CheckQuantifierBindingsAreUsed(const std::string& source) {
  size_t search_from = 0;
  while (search_from < source.size()) {
    const size_t exists_pos = source.find("exists((", search_from);
    const size_t forall_pos = source.find("forall((", search_from);
    if (exists_pos == std::string::npos && forall_pos == std::string::npos) break;

    const bool use_exists =
        forall_pos == std::string::npos || (exists_pos != std::string::npos && exists_pos < forall_pos);
    const size_t quant_pos = use_exists ? exists_pos : forall_pos;
    constexpr size_t kPrefixLen = 8;  // "exists((" / "forall(("

    const size_t bind_start = quant_pos + kPrefixLen;
    const size_t bind_end = source.find(')', bind_start);
    if (bind_end == std::string::npos) {
      search_from = quant_pos + 1;
      continue;
    }

    std::string binding = source.substr(bind_start, bind_end - bind_start);
    if (!use_exists) {
      const size_t in_pos = binding.find(" in ");
      if (in_pos != std::string::npos) {
        binding = binding.substr(0, in_pos);
      }
    }

    const size_t pipe_pos = source.find('|', bind_end);
    if (pipe_pos == std::string::npos) {
      search_from = quant_pos + 1;
      continue;
    }

    const auto body = ExtractQuantifierBody(source, pipe_pos);
    if (!body) {
      search_from = quant_pos + 1;
      continue;
    }

    for (const auto& var : SplitBindingVars(binding)) {
      if (!IdentifierAppearsIn(body->text, var)) {
        return false;
      }
    }

    search_from = body->end_pos + 1;
  }
  return true;
}

bool BindingClauseHasDomain(const std::string& clause) { return clause.find(" in ") != std::string::npos; }

std::vector<std::string> SplitBindingClauses(const std::string& binding) {
  std::vector<std::string> out;
  std::string current;
  auto flush = [&]() {
    while (!current.empty() && std::isspace(static_cast<unsigned char>(current.front()))) {
      current.erase(current.begin());
    }
    while (!current.empty() && std::isspace(static_cast<unsigned char>(current.back()))) {
      current.pop_back();
    }
    if (!current.empty()) out.push_back(current);
    current.clear();
  };
  for (char c : binding) {
    if (c == ',') {
      flush();
      continue;
    }
    current.push_back(c);
  }
  flush();
  return out;
}

bool CheckForallBindingsHaveDomains(const std::string& source) {
  size_t search_from = 0;
  while (search_from < source.size()) {
    const size_t forall_pos = source.find("forall((", search_from);
    if (forall_pos == std::string::npos) break;

    const size_t bind_start = forall_pos + 8;
    const size_t bind_end = source.find(')', bind_start);
    if (bind_end == std::string::npos) return false;

    const std::string binding = source.substr(bind_start, bind_end - bind_start);
    for (const auto& clause : SplitBindingClauses(binding)) {
      if (!BindingClauseHasDomain(clause)) return false;
    }

    search_from = bind_end + 1;
  }
  return true;
}

std::optional<std::string> GenFormula(Rng& rng, size_t budget, const std::vector<std::string>& bound_vars,
                                      const RelationMap& schema, const RelationMap& edb_map,
                                      const GeneratorProfile& profile, const std::unordered_set<std::string>& callable,
                                      bool in_quantifier = false);

std::string FormatSquareBindingHead(int arity) {
  std::ostringstream os;
  os << "[";
  for (int i = 0; i < arity; ++i) {
    if (i > 0) os << ", ";
    os << kVarNames[static_cast<size_t>(i)];
  }
  os << "]";
  return os.str();
}

std::optional<DefBodyResult> GenPartialAppDefBody(Rng& rng, const RelationMap& schema,
                                                  const GeneratorProfile& profile) {
  if (!profile.allow_partial_app) return std::nullopt;
  auto candidates = RelationsWithMaxArity(schema, 3);
  std::vector<RelationPick> partial_candidates;
  for (const auto& candidate : candidates) {
    if (candidate.arity >= 2) partial_candidates.push_back(candidate);
  }
  if (partial_candidates.empty()) return std::nullopt;

  const auto pick = partial_candidates[rng.Uniform(partial_candidates.size())];
  const int supplied = rng.UniformInt(1, pick.arity - 1);
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(supplied));
  for (int i = 0; i < supplied; ++i) {
    args.push_back(kVarNames[static_cast<size_t>(i)]);
  }
  const std::string body = FormatSquareBindingHead(supplied) + ": " + FormatPartialApp(pick.name, args);
  return DefBodyResult{body, supplied};
}

std::optional<DefBodyResult> GenAggregateDefBody(Rng& rng, const RelationMap& schema, const GeneratorProfile& profile) {
  if (!profile.allow_aggregates) return std::nullopt;
  const char* agg = kAggOps[rng.Uniform(std::size(kAggOps))];

  if (profile.allow_partial_app && rng.CoinFlip()) {
    auto candidates = RelationsWithMaxArity(schema, 3);
    std::vector<RelationPick> partial_candidates;
    for (const auto& candidate : candidates) {
      if (candidate.arity >= 2) partial_candidates.push_back(candidate);
    }
    if (!partial_candidates.empty()) {
      const auto& pick = partial_candidates[rng.Uniform(partial_candidates.size())];
      const std::string partial = FormatPartialApp(pick.name, {"x"});
      return DefBodyResult{"[x]: " + std::string(agg) + "[" + partial + "]", 1};
    }
  }

  auto unary = RelationsWithMaxArity(schema, 1);
  if (unary.empty()) return std::nullopt;
  const auto& pick = unary[rng.Uniform(unary.size())];
  return DefBodyResult{std::string(agg) + "[" + pick.name + "]", 1};
}

std::optional<DefBodyResult> GenBindingDefBody(Rng& rng, const RelationMap& schema, const RelationMap& edb_map,
                                               const GeneratorProfile& profile) {
  if (!profile.allow_binding_exprs) return std::nullopt;
  const auto tables = EdbUnaryTables(schema, edb_map);
  if (tables.empty()) return std::nullopt;

  auto candidates = RelationsWithMaxArity(schema, 3);
  std::vector<RelationPick> valid;
  for (const auto& candidate : candidates) {
    if (candidate.arity >= 2) valid.push_back(candidate);
  }
  if (valid.empty()) return std::nullopt;

  const auto& pick = valid[rng.Uniform(valid.size())];
  const std::string table = tables[rng.Uniform(tables.size())];

  if (pick.arity == 2) {
    if (profile.allow_partial_app && rng.CoinFlip()) {
      return DefBodyResult{"[x in " + table + "]: " + FormatPartialApp(pick.name, {"x"}), 1};
    }
    return DefBodyResult{"[x in " + table + ", y]: " + FormatApp(pick.name, {"x", "y"}), 2};
  }

  const std::string table2 = tables[rng.Uniform(tables.size())];
  return DefBodyResult{"[x in " + table + ", y in " + table2 + ", z]: " + FormatApp(pick.name, {"x", "y", "z"}), 3};
}

std::optional<DefBodyResult> GenWhereDefBody(Rng& rng, const RelationMap& schema, const RelationMap& edb_map,
                                             const GeneratorProfile& profile) {
  if (!profile.allow_where || !profile.allow_partial_app) return std::nullopt;
  const auto unary = EdbUnaryTables(schema, edb_map);
  if (unary.empty()) return std::nullopt;

  auto candidates = RelationsWithMaxArity(schema, 2);
  std::vector<RelationPick> binary;
  for (const auto& candidate : candidates) {
    if (candidate.arity >= 2) binary.push_back(candidate);
  }
  if (binary.empty()) return std::nullopt;

  const auto& rel = binary[rng.Uniform(binary.size())];
  const std::string domain = unary[rng.Uniform(unary.size())];
  const std::string partial = FormatPartialApp(rel.name, {"x"});
  const std::string filter = FormatApp(domain, {"x"});
  return DefBodyResult{"[x]: " + partial + " where " + filter, 1};
}

std::optional<std::string> GenFormula(Rng& rng, size_t budget, const std::vector<std::string>& bound_vars,
                                      const RelationMap& schema, const RelationMap& edb_map,
                                      const GeneratorProfile& profile, const std::unordered_set<std::string>& callable,
                                      bool in_quantifier) {
  if (budget == 0 || bound_vars.empty()) return std::nullopt;
  const bool plain_atoms = in_quantifier;
  if (budget == 1) return PickAtom(rng, schema, bound_vars, profile, callable, plain_atoms);

  enum class Choice { kAnd, kOr, kExists, kForall, kAtom, kComparison, kNot };
  std::vector<Choice> choices = {Choice::kAnd, Choice::kAtom};
  if (!in_quantifier) {
    if (budget >= 3) choices.push_back(Choice::kExists);
    if (profile.allow_forall && budget >= 4 && !EdbUnaryTables(schema, edb_map).empty()) {
      choices.push_back(Choice::kForall);
      if (profile.focus_quantifiers) {
        choices.push_back(Choice::kForall);
      }
    }
    if (budget >= 2 && !bound_vars.empty()) choices.push_back(Choice::kOr);
    if (profile.allow_comparisons && budget >= 2) choices.push_back(Choice::kComparison);
    if (profile.allow_negation && budget >= 2) choices.push_back(Choice::kNot);
  } else {
    // Quantifier bodies must stay free of arithmetic/comparisons for Rel groundness.
    choices.clear();
    choices.push_back(Choice::kAnd);
    choices.push_back(Choice::kAtom);
  }

  const Choice choice = choices[rng.Uniform(choices.size())];

  switch (choice) {
    case Choice::kAtom:
      return PickAtom(rng, schema, bound_vars, profile, callable, plain_atoms);
    case Choice::kComparison: {
      const std::string atom = PickAtom(rng, schema, bound_vars, profile, callable, plain_atoms);
      const std::string cmp = GenComparison(rng, bound_vars, profile);
      return "(" + atom + " and " + cmp + ")";
    }
    case Choice::kNot: {
      const std::string atom = PickAtom(rng, schema, bound_vars, profile, callable, plain_atoms);
      return "(not " + atom + ")";
    }
    case Choice::kAnd: {
      size_t left_budget = 1 + rng.Uniform(budget - 1);
      size_t right_budget = budget - 1 - left_budget;
      if (right_budget == 0) {
        right_budget = 1;
        left_budget = budget - 2;
      }
      auto lhs = GenFormula(rng, left_budget, bound_vars, schema, edb_map, profile, callable, in_quantifier);
      auto rhs = GenFormula(rng, right_budget, bound_vars, schema, edb_map, profile, callable, in_quantifier);
      if (!lhs || !rhs) return PickAtom(rng, schema, bound_vars, profile, callable, plain_atoms);
      return "(" + *lhs + " and " + *rhs + ")";
    }
    case Choice::kOr: {
      if (budget < 4) return PickAtom(rng, schema, bound_vars, profile, callable, plain_atoms);
      size_t left_budget = 1 + rng.Uniform(budget - 3);
      size_t right_budget = budget - 2 - left_budget;
      if (right_budget == 0) {
        right_budget = 1;
        left_budget = budget - 3;
      }
      auto lhs = GenFormula(rng, left_budget, bound_vars, schema, edb_map, profile, callable, in_quantifier);
      auto rhs = GenFormula(rng, right_budget, bound_vars, schema, edb_map, profile, callable, in_quantifier);
      if (!lhs || !rhs) return PickAtom(rng, schema, bound_vars, profile, callable, plain_atoms);
      const std::string lhs_atom = PickAtomGroundingAll(rng, schema, bound_vars, callable);
      const std::string rhs_atom = PickAtomGroundingAll(rng, schema, bound_vars, callable);
      return "((" + lhs_atom + " and " + *lhs + ") or (" + rhs_atom + " and " + *rhs + "))";
    }
    case Choice::kExists: {
      if (bound_vars.size() >= kVarNames.size()) {
        return PickAtom(rng, schema, bound_vars, profile, callable, plain_atoms);
      }
      const std::string fresh = kVarNames[bound_vars.size()];
      std::vector<std::string> inner_vars = bound_vars;
      inner_vars.push_back(fresh);

      const std::string fresh_atom = PickAtomIncluding(rng, schema, fresh, bound_vars, profile, callable, true);
      const std::string outer_atom = PickAtomGroundingAll(rng, schema, bound_vars, callable);
      if (budget <= 3) {
        return "(" + outer_atom + " and exists((" + fresh + ") | (" + fresh_atom + ")))";
      }
      auto body = GenFormula(rng, budget - 3, inner_vars, schema, edb_map, profile, callable, true);
      if (!body) {
        return "(" + outer_atom + " and exists((" + fresh + ") | (" + fresh_atom + ")))";
      }
      return "(" + outer_atom + " and exists((" + fresh + ") | (" + fresh_atom + " and " + *body + ")))";
    }
    case Choice::kForall: {
      auto tables = EdbUnaryTables(schema, edb_map);
      if (tables.empty() || bound_vars.size() >= kVarNames.size()) {
        return PickAtom(rng, schema, bound_vars, profile, callable, plain_atoms);
      }
      const std::string fresh = kVarNames[bound_vars.size()];
      const std::string table = tables[rng.Uniform(tables.size())];
      std::vector<std::string> inner_vars = bound_vars;
      inner_vars.push_back(fresh);

      const std::string fresh_atom = PickAtomIncluding(rng, schema, fresh, bound_vars, profile, callable, true);
      const std::string outer_atom = PickAtomGroundingAll(rng, schema, bound_vars, callable);
      if (budget <= 4) {
        return "(" + outer_atom + " and forall((" + fresh + " in " + table + ") | (" + fresh_atom + ")))";
      }
      auto body = GenFormula(rng, budget - 4, inner_vars, schema, edb_map, profile, callable, true);
      if (!body) {
        return "(" + outer_atom + " and forall((" + fresh + " in " + table + ") | (" + fresh_atom + ")))";
      }
      return "(" + outer_atom + " and forall((" + fresh + " in " + table + ") | (" + fresh_atom + " and " + *body +
             ")))";
    }
  }
  return PickAtom(rng, schema, bound_vars, profile, callable, plain_atoms);
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
                               const RelationMap& edb_map, const GeneratorProfile& profile,
                               const std::unordered_set<std::string>& callable) {
  const auto bound_vars = BindingVars(arity);
  auto formula = GenFormula(rng, budget, bound_vars, schema, edb_map, profile, callable);
  if (!formula) formula = PickAtom(rng, schema, bound_vars, profile, callable);
  return GenBindingHead(arity) + ": " + *formula;
}

std::vector<RelationPick> PartialAppCandidates(const RelationMap& schema) {
  std::vector<RelationPick> out;
  for (const auto& candidate : RelationsWithMaxArity(schema, 3)) {
    if (candidate.arity >= 2) out.push_back(candidate);
  }
  return out;
}

std::vector<RelationPick> BinaryRelationCandidates(const RelationMap& schema) {
  std::vector<RelationPick> out;
  for (const auto& candidate : RelationsWithMaxArity(schema, 3)) {
    if (candidate.arity == 2) out.push_back(candidate);
  }
  return out;
}

std::optional<DefBodyResult> GenProductExprDefBody(Rng& rng, const RelationMap& schema,
                                                   const GeneratorProfile& profile) {
  if (!profile.allow_products) return std::nullopt;

  if (!profile.allow_partial_app || rng.CoinFlip()) {
    const int a = rng.UniformInt(1, 5);
    const int b = rng.UniformInt(1, 5);
    return DefBodyResult{"(" + std::to_string(a) + ", " + std::to_string(b) + ")", 2};
  }

  const auto partial_candidates = PartialAppCandidates(schema);
  if (partial_candidates.empty()) return std::nullopt;

  if (rng.CoinFlip()) {
    const auto& left = partial_candidates[rng.Uniform(partial_candidates.size())];
    auto unary = RelationsWithMaxArity(schema, 1);
    if (unary.empty()) return std::nullopt;
    const auto& right = unary[rng.Uniform(unary.size())];
    const std::string expr = "(" + FormatPartialApp(left.name, {"x"}) + ", " + FormatApp(right.name, {"x"}) + ")";
    return DefBodyResult{"[x]: " + expr, 2};
  }

  const auto& left = partial_candidates[rng.Uniform(partial_candidates.size())];
  const auto& right = partial_candidates[rng.Uniform(partial_candidates.size())];
  const std::string expr = "(" + FormatPartialApp(left.name, {"x"}) + ", " + FormatPartialApp(right.name, {"y"}) + ")";
  return DefBodyResult{"[x, y]: " + expr, 2};
}

std::optional<DefBodyResult> GenUnionExprDefBody(Rng& rng, const RelationMap& schema, const GeneratorProfile& profile) {
  if (!profile.allow_extensional) return std::nullopt;

  enum class UnionStyle { kScalarLiterals, kTupleLiterals, kPartialApps, kFormulaOverUnion };
  std::vector<UnionStyle> styles = {UnionStyle::kScalarLiterals, UnionStyle::kTupleLiterals};
  if (profile.allow_partial_app) {
    styles.push_back(UnionStyle::kPartialApps);
    styles.push_back(UnionStyle::kFormulaOverUnion);
  }

  switch (styles[rng.Uniform(styles.size())]) {
    case UnionStyle::kScalarLiterals: {
      const int branches = rng.UniformInt(2, 3);
      std::ostringstream os;
      for (int i = 0; i < branches; ++i) {
        if (i > 0) os << "; ";
        os << rng.UniformInt(1, 5);
      }
      return DefBodyResult{"{" + os.str() + "}", 1};
    }
    case UnionStyle::kTupleLiterals: {
      const int branches = 2;
      std::ostringstream os;
      for (int i = 0; i < branches; ++i) {
        if (i > 0) os << "; ";
        os << "(" << rng.UniformInt(1, 5) << ", " << rng.UniformInt(1, 5) << ")";
      }
      return DefBodyResult{"{" + os.str() + "}", 2};
    }
    case UnionStyle::kPartialApps: {
      const auto binary = BinaryRelationCandidates(schema);
      if (binary.empty()) return std::nullopt;
      const auto& rel = binary[rng.Uniform(binary.size())];
      const int c1 = rng.UniformInt(1, 5);
      int c2 = rng.UniformInt(1, 5);
      if (c2 == c1) c2 = c1 == 5 ? 1 : c1 + 1;
      const std::string union_expr = "{" + FormatPartialApp(rel.name, {std::to_string(c1)}) + "; " +
                                     FormatPartialApp(rel.name, {std::to_string(c2)}) + "}";
      return DefBodyResult{union_expr, 1};
    }
    case UnionStyle::kFormulaOverUnion:
    default: {
      const auto binary = BinaryRelationCandidates(schema);
      if (binary.empty()) return std::nullopt;
      const auto& rel = binary[rng.Uniform(binary.size())];
      const int c1 = rng.UniformInt(1, 5);
      int c2 = rng.UniformInt(1, 5);
      if (c2 == c1) c2 = c1 == 5 ? 1 : c1 + 1;
      const std::string union_expr = "{" + FormatPartialApp(rel.name, {std::to_string(c1)}) + "; " +
                                     FormatPartialApp(rel.name, {std::to_string(c2)}) + "}";
      return DefBodyResult{"(x): " + union_expr + "(x)", 1};
    }
  }
}

std::optional<DefBodyResult> GenForallDefBody(Rng& rng, size_t /*budget*/, const RelationMap& schema,
                                              const RelationMap& edb_map, const GeneratorProfile& profile,
                                              const std::unordered_set<std::string>& callable) {
  if (!profile.allow_forall) return std::nullopt;
  const auto tables = EdbUnaryTables(schema, edb_map);
  if (tables.empty()) return std::nullopt;

  const int arity = rng.UniformInt(1, 2);
  const auto bound_vars = BindingVars(arity);
  const std::string outer_atom = PickAtomGroundingAll(rng, schema, bound_vars, callable);

  if (arity == 1 && tables.size() >= 2 && rng.CoinFlip()) {
    std::vector<RelationPick> triples;
    for (const auto& candidate : RelationsWithMaxArity(schema, 3)) {
      if (candidate.arity == 3) triples.push_back(candidate);
    }
    if (!triples.empty()) {
      const auto& rel = triples[rng.Uniform(triples.size())];
      const std::string t1 = tables[rng.Uniform(tables.size())];
      const std::string t2 = tables[rng.Uniform(tables.size())];
      const std::string inner = FormatApp(rel.name, {"x", "y", "z"});
      return DefBodyResult{GenBindingHead(arity) + ": (" + outer_atom + " and forall((y in " + t1 + ", z in " + t2 +
                               ") | " + inner + "))",
                           arity};
    }
  }

  const std::string table = tables[rng.Uniform(tables.size())];
  const std::string quant_var = arity == 1 ? "y" : "z";
  std::vector<std::string> inner_pool = bound_vars;
  inner_pool.push_back(quant_var);
  const std::string inner_atom = PickAtomIncluding(rng, schema, quant_var, inner_pool, profile, callable, true);
  return DefBodyResult{GenBindingHead(arity) + ": (" + outer_atom + " and forall((" + quant_var + " in " + table +
                           ") | " + inner_atom + "))",
                       arity};
}

enum class BodyKind {
  kIntensional,
  kPartialExpr,
  kAggregateExpr,
  kBindingExpr,
  kWhereExpr,
  kExtensional,
  kJoinProduct,
  kProductExpr,
  kUnionExpr,
  kForallBody,
};

std::vector<BodyKind> CollectBodyKinds(const GeneratorProfile& profile, size_t body_budget) {
  if (profile.focus_quantifiers) {
    return {BodyKind::kForallBody, BodyKind::kIntensional};
  }
  if (profile.focus_expr_algebra) {
    std::vector<BodyKind> kinds = {BodyKind::kProductExpr, BodyKind::kUnionExpr};
    if (profile.allow_partial_app && body_budget >= 2) kinds.push_back(BodyKind::kPartialExpr);
    if (profile.allow_extensional && body_budget >= 2) kinds.push_back(BodyKind::kExtensional);
    return kinds;
  }
  if (profile.focus_aggregates) {
    std::vector<BodyKind> kinds = {BodyKind::kAggregateExpr};
    if (profile.allow_partial_app && body_budget >= 2) kinds.push_back(BodyKind::kPartialExpr);
    return kinds;
  }

  std::vector<BodyKind> kinds = {BodyKind::kIntensional};
  if (profile.allow_partial_app && body_budget >= 2) kinds.push_back(BodyKind::kPartialExpr);
  if (profile.allow_aggregates && body_budget >= 2) kinds.push_back(BodyKind::kAggregateExpr);
  if (profile.allow_binding_exprs && body_budget >= 3) kinds.push_back(BodyKind::kBindingExpr);
  if (profile.allow_where && body_budget >= 3) kinds.push_back(BodyKind::kWhereExpr);
  if (profile.allow_extensional && body_budget >= 2) kinds.push_back(BodyKind::kExtensional);
  if (profile.allow_products && body_budget >= 3) {
    kinds.push_back(BodyKind::kJoinProduct);
    kinds.push_back(BodyKind::kProductExpr);
  }
  if (profile.allow_extensional && body_budget >= 2) kinds.push_back(BodyKind::kUnionExpr);
  if (profile.allow_forall && body_budget >= 4) kinds.push_back(BodyKind::kForallBody);
  return kinds;
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

std::string GenProductBody(Rng& rng, size_t /*budget*/, const RelationMap& schema, const RelationMap& edb_map) {
  auto unary = EdbUnaryTables(schema, edb_map);
  if (unary.size() < 2) {
    return "(x, y): A(x) and B(y)";
  }
  const std::string left = unary[rng.Uniform(unary.size())];
  const std::string right = unary[rng.Uniform(unary.size())];
  return "(x, y): " + FormatApp(left, {"x"}) + " and " + FormatApp(right, {"y"});
}

std::optional<std::string> PickBinaryEdb(Rng& rng, const RelationMap& edb_map) {
  std::vector<std::string> names;
  for (const auto& kv : edb_map.map) {
    if (kv.second.arity == 2) names.push_back(kv.first);
  }
  if (names.empty()) return std::nullopt;
  std::sort(names.begin(), names.end());
  return names[rng.Uniform(names.size())];
}

std::string GenRecursiveBody(const std::string& self_name, const std::string& base_rel) {
  return "(x, y): " + base_rel + "(x, y) or exists((z) | " + base_rel + "(x, z) and " + self_name + "(z, y))";
}

std::string MakeIdbName(int index) { return "Gen" + std::to_string(index); }

GeneratedProgram TryGenerateOnce(const SuiteConfig& config, uint64_t attempt) {
  Rng rng(ProgramGenerationSeed(config.seed, config.program_index, config.node_budget, attempt));

  RelationMap schema = config.edb_map;

  std::vector<size_t> body_budgets(1, 1);
  const size_t overhead = 1;
  size_t formula_budget = config.node_budget > overhead ? config.node_budget - overhead : 1;
  formula_budget = std::max(formula_budget, size_t{1});
  const size_t extra = formula_budget - 1;
  for (size_t i = 0; i < extra; ++i) {
    body_budgets[0]++;
  }

  std::ostringstream program;

  std::unordered_set<std::string> callable_relations;
  for (const auto& kv : config.edb_map.map) callable_relations.insert(kv.first);

  const std::string name = MakeIdbName(0);

  std::string body;
  int arity = 1;

  const std::vector<BodyKind> kinds = CollectBodyKinds(config.profile, body_budgets[0]);
  const BodyKind kind = kinds[rng.Uniform(kinds.size())];
  switch (kind) {
    case BodyKind::kExtensional:
      arity = rng.UniformInt(1, 3);
      body = GenExtensionalBody(rng, body_budgets[0], arity);
      break;
    case BodyKind::kJoinProduct:
      arity = 2;
      body = GenProductBody(rng, body_budgets[0], schema, config.edb_map);
      break;
    case BodyKind::kProductExpr:
      if (auto product_expr = GenProductExprDefBody(rng, schema, config.profile)) {
        body = product_expr->body;
        arity = product_expr->arity;
      } else {
        arity = rng.UniformInt(1, 3);
        body =
            GenIntensionalBody(rng, body_budgets[0], arity, schema, config.edb_map, config.profile, callable_relations);
      }
      break;
    case BodyKind::kUnionExpr:
      if (auto union_expr = GenUnionExprDefBody(rng, schema, config.profile)) {
        body = union_expr->body;
        arity = union_expr->arity;
      } else {
        arity = rng.UniformInt(1, 3);
        body =
            GenIntensionalBody(rng, body_budgets[0], arity, schema, config.edb_map, config.profile, callable_relations);
      }
      break;
    case BodyKind::kForallBody:
      if (auto forall_body =
              GenForallDefBody(rng, body_budgets[0], schema, config.edb_map, config.profile, callable_relations)) {
        body = forall_body->body;
        arity = forall_body->arity;
      } else {
        arity = rng.UniformInt(1, 2);
        body =
            GenIntensionalBody(rng, body_budgets[0], arity, schema, config.edb_map, config.profile, callable_relations);
      }
      break;
    case BodyKind::kPartialExpr:
      if (auto partial = GenPartialAppDefBody(rng, schema, config.profile)) {
        body = partial->body;
        arity = partial->arity;
      } else {
        arity = rng.UniformInt(1, 3);
        body =
            GenIntensionalBody(rng, body_budgets[0], arity, schema, config.edb_map, config.profile, callable_relations);
      }
      break;
    case BodyKind::kAggregateExpr:
      if (auto aggregate = GenAggregateDefBody(rng, schema, config.profile)) {
        body = aggregate->body;
        arity = aggregate->arity;
      } else {
        arity = rng.UniformInt(1, 3);
        body =
            GenIntensionalBody(rng, body_budgets[0], arity, schema, config.edb_map, config.profile, callable_relations);
      }
      break;
    case BodyKind::kBindingExpr:
      if (auto binding = GenBindingDefBody(rng, schema, config.edb_map, config.profile)) {
        body = binding->body;
        arity = binding->arity;
      } else {
        arity = rng.UniformInt(1, 3);
        body =
            GenIntensionalBody(rng, body_budgets[0], arity, schema, config.edb_map, config.profile, callable_relations);
      }
      break;
    case BodyKind::kWhereExpr:
      if (auto where = GenWhereDefBody(rng, schema, config.edb_map, config.profile)) {
        body = where->body;
        arity = where->arity;
      } else {
        arity = rng.UniformInt(1, 3);
        body =
            GenIntensionalBody(rng, body_budgets[0], arity, schema, config.edb_map, config.profile, callable_relations);
      }
      break;
    case BodyKind::kIntensional:
    default:
      arity = rng.UniformInt(1, 3);
      body =
          GenIntensionalBody(rng, body_budgets[0], arity, schema, config.edb_map, config.profile, callable_relations);
      break;
  }

  program << "def " << name << " {" << body << "}";
  schema[name] = RelationInfo(arity);

  GeneratedProgram result;
  result.source = program.str();
  result.schema = schema;
  result.output_def = name;
  return result;
}

class FullApplicationArityCheckVisitor : public BaseRelVisitor {
 public:
  explicit FullApplicationArityCheckVisitor(const RelContextBuilder& container) : container_(container) {}

  using BaseRelVisitor::Visit;

  bool ok() const { return ok_; }

  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullApplication>& node) override {
    if (auto* id_base = dynamic_cast<RelIDApplBase*>(node->base.get())) {
      if (GetAggregateMap().find(id_base->id) == GetAggregateMap().end()) {
        const auto info = container_.GetRelationInfo(id_base->id);
        if (info) {
          int supplied = 0;
          for (const auto& param : node->params) {
            if (!param) continue;
            if (auto expr = param->GetExpr()) {
              supplied += static_cast<int>(expr->arity);
            }
          }
          if (supplied != info->arity) {
            ok_ = false;
          }
        }
      }
    }
    return BaseRelVisitor::Visit(node);
  }

 private:
  const RelContextBuilder& container_;
  bool ok_ = true;
};

bool FullApplicationsMatchArity(const std::string& source, const RelationMap& edb_map) {
  auto parser = rel2sql::GetParser(source);
  auto tree = parser->program();
  RelASTBuilder ast_builder;
  auto root = ast_builder.Build(tree);
  RelContextBuilder context_builder(edb_map);
  context_builder.SetRoot(root);
  IDsVisitor ids_visitor(&context_builder);
  ids_visitor.Visit(root);
  ArityVisitor arity_visitor(&context_builder);
  arity_visitor.Visit(root);

  FullApplicationArityCheckVisitor checker(context_builder);
  checker.Visit(root);
  return checker.ok();
}

}  // namespace

bool ProgramContainsStaticallyFalseComparison(const std::string& source) {
  return ContainsStaticallyFalseComparison(source);
}

std::optional<std::string> ProgramOutputDefName(const std::string& source) { return OutputDefNameFromSource(source); }

bool ProgramHasConnectedDependencyGraph(const std::string& source) {
  const auto output_name = OutputDefNameFromSource(source);
  if (!output_name || !ExtractDefBody(source, *output_name)) return false;

  const auto intermediates = IntermediateDefNames(source);
  if (intermediates.empty()) return true;

  std::unordered_map<std::string, std::vector<std::string>> deps;
  if (auto output_body = ExtractDefBody(source, *output_name)) {
    deps[*output_name] = GenIdRefsInBody(*output_body);
    if (deps[*output_name].empty()) return false;
  }

  for (const auto& gen : intermediates) {
    const auto body = ExtractDefBody(source, gen);
    if (!body) return false;
    deps[gen] = GenIdRefsInBody(*body);
  }

  std::unordered_set<std::string> reachable;
  std::vector<std::string> stack = {*output_name};
  while (!stack.empty()) {
    const std::string cur = stack.back();
    stack.pop_back();
    const auto it = deps.find(cur);
    if (it == deps.end()) continue;
    for (const auto& ref : it->second) {
      if (!reachable.insert(ref).second) continue;
      stack.push_back(ref);
    }
  }

  for (const auto& gen : intermediates) {
    if (!reachable.count(gen)) return false;
  }
  return true;
}

bool VerifyProgram(const std::string& source, const RelationMap& edb_map) {
  if (ContainsStaticallyFalseComparison(source)) return false;
  if (!OutputDefNameFromSource(source)) return false;
  if (!ProgramHasConnectedDependencyGraph(source)) return false;
  if (!CheckForallBindingsHaveDomains(source)) return false;
  try {
    if (!FullApplicationsMatchArity(source, edb_map)) return false;
    auto sql = GetSQLRel(source, edb_map);
    return sql != nullptr;
  } catch (...) {
    return false;
  }
}

bool QuantifierBindingsAreUsed(const std::string& source) { return CheckQuantifierBindingsAreUsed(source); }

bool ForallBindingsHaveDomains(const std::string& source) { return CheckForallBindingsHaveDomains(source); }

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
