#include "generator/corpus.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "generator/data_fixture.h"
#include "generator/raicode_paths.h"
#include "generator/rel_engine_json.h"

namespace rel2sql::generator {
namespace {

void SkipWs(const std::string& s, size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

bool MatchLiteral(const std::string& s, size_t& i, const char* lit) {
  const size_t n = std::char_traits<char>::length(lit);
  if (s.compare(i, n, lit) != 0) return false;
  i += n;
  return true;
}

std::string ParseJsonString(const std::string& s, size_t& i) {
  if (i >= s.size() || s[i] != '"') throw std::runtime_error("expected JSON string");
  ++i;
  std::string out;
  while (i < s.size()) {
    char c = s[i++];
    if (c == '"') return out;
    if (c == '\\') {
      if (i >= s.size()) throw std::runtime_error("bad JSON escape");
      char e = s[i++];
      switch (e) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        default:
          out.push_back(e);
          break;
      }
      continue;
    }
    out.push_back(c);
  }
  throw std::runtime_error("unterminated JSON string");
}

void ExpectChar(const std::string& s, size_t& i, char expected) {
  SkipWs(s, i);
  if (i >= s.size() || s[i] != expected) throw std::runtime_error("JSON parse error");
  ++i;
}

void SkipValue(const std::string& s, size_t& i) {
  SkipWs(s, i);
  if (i >= s.size()) return;
  if (s[i] == '"') {
    (void)ParseJsonString(s, i);
    return;
  }
  if (s[i] == '{') {
    int depth = 0;
    while (i < s.size()) {
      if (s[i] == '{') ++depth;
      if (s[i] == '}') {
        --depth;
        ++i;
        if (depth == 0) break;
      } else {
        ++i;
      }
    }
    return;
  }
  if (s[i] == '[') {
    int depth = 0;
    while (i < s.size()) {
      if (s[i] == '[') ++depth;
      if (s[i] == ']') {
        --depth;
        ++i;
        if (depth == 0) break;
      } else {
        ++i;
      }
    }
    return;
  }
  while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;
}

double ParseJsonNumber(const std::string& s, size_t& i) {
  SkipWs(s, i);
  size_t start = i;
  while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.' ||
                          s[i] == 'e' || s[i] == 'E')) {
    ++i;
  }
  return std::stod(s.substr(start, i - start));
}

bool ParseJsonBool(const std::string& s, size_t& i) {
  SkipWs(s, i);
  if (MatchLiteral(s, i, "true")) return true;
  if (MatchLiteral(s, i, "false")) return false;
  throw std::runtime_error("expected JSON bool");
}

GeneratorProfile ParseProfile(const std::string& s, size_t& i) {
  GeneratorProfile profile;
  ExpectChar(s, i, '{');
  while (true) {
    SkipWs(s, i);
    if (MatchLiteral(s, i, "}")) break;
    const std::string key = ParseJsonString(s, i);
    ExpectChar(s, i, ':');
    SkipWs(s, i);
    if (key == "allow_recursion")
      profile.allow_recursion = ParseJsonBool(s, i);
    else if (key == "allow_forall")
      profile.allow_forall = ParseJsonBool(s, i);
    else if (key == "allow_aggregates")
      profile.allow_aggregates = ParseJsonBool(s, i);
    else if (key == "allow_extensional")
      profile.allow_extensional = ParseJsonBool(s, i);
    else if (key == "allow_partial_app")
      profile.allow_partial_app = ParseJsonBool(s, i);
    else if (key == "allow_products")
      profile.allow_products = ParseJsonBool(s, i);
    else if (key == "allow_arithmetic")
      profile.allow_arithmetic = ParseJsonBool(s, i);
    else if (key == "allow_comparisons")
      profile.allow_comparisons = ParseJsonBool(s, i);
    else if (key == "allow_negation")
      profile.allow_negation = ParseJsonBool(s, i);
    else if (key == "allow_where")
      profile.allow_where = ParseJsonBool(s, i);
    else if (key == "allow_binding_exprs")
      profile.allow_binding_exprs = ParseJsonBool(s, i);
    else if (key == "max_defs")
      profile.max_defs = static_cast<int>(ParseJsonNumber(s, i));
    else if (key == "focus_quantifiers")
      profile.focus_quantifiers = ParseJsonBool(s, i);
    else if (key == "focus_expr_algebra")
      profile.focus_expr_algebra = ParseJsonBool(s, i);
    else if (key == "focus_aggregates")
      profile.focus_aggregates = ParseJsonBool(s, i);
    else
      SkipValue(s, i);
    SkipWs(s, i);
    if (MatchLiteral(s, i, ",")) continue;
    if (s[i] == '}') {
      ++i;
      break;
    }
  }
  return profile;
}

SuiteConfig ParseConfig(const std::string& s, size_t& i) {
  SuiteConfig config;
  ExpectChar(s, i, '{');
  while (true) {
    SkipWs(s, i);
    if (MatchLiteral(s, i, "}")) break;
    const std::string key = ParseJsonString(s, i);
    ExpectChar(s, i, ':');
    SkipWs(s, i);
    if (key == "seed")
      config.seed = static_cast<uint64_t>(ParseJsonNumber(s, i));
    else if (key == "program_index")
      config.program_index = static_cast<size_t>(ParseJsonNumber(s, i));
    else if (key == "node_budget")
      config.node_budget = static_cast<size_t>(ParseJsonNumber(s, i));
    else if (key == "profile")
      config.profile = ParseProfile(s, i);
    else if (key == "profile_name")
      SkipValue(s, i);
    else
      SkipValue(s, i);
    SkipWs(s, i);
    if (MatchLiteral(s, i, ",")) continue;
    if (s[i] == '}') {
      ++i;
      break;
    }
  }
  return config;
}

RelationMap ParseSchema(const std::string& s, size_t& i) {
  RelationMap schema;
  ExpectChar(s, i, '{');
  while (true) {
    SkipWs(s, i);
    if (MatchLiteral(s, i, "}")) break;
    const std::string name = ParseJsonString(s, i);
    ExpectChar(s, i, ':');
    const int arity = static_cast<int>(ParseJsonNumber(s, i));
    schema[name] = RelationInfo(arity);
    SkipWs(s, i);
    if (MatchLiteral(s, i, ",")) continue;
    if (s[i] == '}') {
      ++i;
      break;
    }
  }
  return schema;
}

TableRows ParseNumericRows(const std::string& s, size_t& i) {
  TableRows rows;
  ExpectChar(s, i, '[');
  SkipWs(s, i);
  if (MatchLiteral(s, i, "]")) return rows;
  while (true) {
    ExpectChar(s, i, '[');
    std::vector<double> row;
    SkipWs(s, i);
    if (!MatchLiteral(s, i, "]")) {
      while (true) {
        row.push_back(ParseJsonNumber(s, i));
        SkipWs(s, i);
        if (MatchLiteral(s, i, "]")) break;
        ExpectChar(s, i, ',');
      }
    }
    rows.push_back(std::move(row));
    SkipWs(s, i);
    if (MatchLiteral(s, i, "]")) break;
    ExpectChar(s, i, ',');
  }
  return rows;
}

std::unordered_map<std::string, TableRows> ParseEdb(const std::string& s, size_t& i) {
  std::unordered_map<std::string, TableRows> edb;
  ExpectChar(s, i, '{');
  while (true) {
    SkipWs(s, i);
    if (MatchLiteral(s, i, "}")) break;
    const std::string name = ParseJsonString(s, i);
    ExpectChar(s, i, ':');
    edb[name] = ParseNumericRows(s, i);
    SkipWs(s, i);
    if (MatchLiteral(s, i, ",")) continue;
    if (s[i] == '}') {
      ++i;
      break;
    }
  }
  return edb;
}

ResultSet ParseExpected(const std::string& s, size_t& i) {
  ResultSet out;
  ExpectChar(s, i, '{');
  while (true) {
    SkipWs(s, i);
    if (MatchLiteral(s, i, "}")) break;
    const std::string key = ParseJsonString(s, i);
    ExpectChar(s, i, ':');
    SkipWs(s, i);
    if (key == "columns") {
      size_t j = i;
      while (j > 0 && s[j - 1] != '[') --j;
      out.column_names.clear();
      ExpectChar(s, i, '[');
      SkipWs(s, i);
      if (!MatchLiteral(s, i, "]")) {
        while (true) {
          out.column_names.push_back(ParseJsonString(s, i));
          SkipWs(s, i);
          if (MatchLiteral(s, i, "]")) break;
          ExpectChar(s, i, ',');
        }
      }
    } else if (key == "rows") {
      const auto row_strings = [&]() {
        std::vector<std::vector<std::string>> rows;
        ExpectChar(s, i, '[');
        SkipWs(s, i);
        if (MatchLiteral(s, i, "]")) return rows;
        while (true) {
          rows.push_back([&]() {
            std::vector<std::string> row;
            ExpectChar(s, i, '[');
            SkipWs(s, i);
            if (MatchLiteral(s, i, "]")) return row;
            while (true) {
              row.push_back(ParseJsonString(s, i));
              SkipWs(s, i);
              if (MatchLiteral(s, i, "]")) break;
              ExpectChar(s, i, ',');
            }
            return row;
          }());
          SkipWs(s, i);
          if (MatchLiteral(s, i, "]")) break;
          ExpectChar(s, i, ',');
        }
        return rows;
      }();
      out.rows.reserve(row_strings.size());
      for (const auto& values : row_strings) {
        Row row;
        row.values = values;
        out.rows.push_back(std::move(row));
      }
    } else {
      SkipValue(s, i);
    }
    SkipWs(s, i);
    if (MatchLiteral(s, i, ",")) continue;
    if (s[i] == '}') {
      ++i;
      break;
    }
  }
  return Canonicalize(std::move(out));
}

std::string SerializeNumericRows(const TableRows& rows) {
  std::ostringstream os;
  os << "[";
  for (size_t r = 0; r < rows.size(); ++r) {
    if (r > 0) os << ",";
    os << "[";
    for (size_t c = 0; c < rows[r].size(); ++c) {
      if (c > 0) os << ",";
      os << rows[r][c];
    }
    os << "]";
  }
  os << "]";
  return os.str();
}

std::string SerializeProfile(const GeneratorProfile& profile) {
  std::ostringstream os;
  os << "{";
  os << "\"allow_recursion\":" << (profile.allow_recursion ? "true" : "false");
  os << ",\"allow_forall\":" << (profile.allow_forall ? "true" : "false");
  os << ",\"allow_aggregates\":" << (profile.allow_aggregates ? "true" : "false");
  os << ",\"allow_extensional\":" << (profile.allow_extensional ? "true" : "false");
  os << ",\"allow_partial_app\":" << (profile.allow_partial_app ? "true" : "false");
  os << ",\"allow_products\":" << (profile.allow_products ? "true" : "false");
  os << ",\"allow_arithmetic\":" << (profile.allow_arithmetic ? "true" : "false");
  os << ",\"allow_comparisons\":" << (profile.allow_comparisons ? "true" : "false");
  os << ",\"allow_negation\":" << (profile.allow_negation ? "true" : "false");
  os << ",\"allow_where\":" << (profile.allow_where ? "true" : "false");
  os << ",\"allow_binding_exprs\":" << (profile.allow_binding_exprs ? "true" : "false");
  os << ",\"max_defs\":" << profile.max_defs;
  os << ",\"focus_quantifiers\":" << (profile.focus_quantifiers ? "true" : "false");
  os << ",\"focus_expr_algebra\":" << (profile.focus_expr_algebra ? "true" : "false");
  os << ",\"focus_aggregates\":" << (profile.focus_aggregates ? "true" : "false");
  os << "}";
  return os.str();
}

std::string SerializeSchema(const RelationMap& schema) {
  std::ostringstream os;
  os << "{";
  bool first = true;
  for (const auto& kv : schema.map) {
    if (!first) os << ",";
    first = false;
    os << "\"" << JsonEscape(kv.first) << "\":" << kv.second.arity;
  }
  os << "}";
  return os.str();
}

std::string SerializeExpected(const ResultSet& expected) {
  std::ostringstream os;
  os << "{\"columns\":[";
  for (size_t i = 0; i < expected.column_names.size(); ++i) {
    if (i > 0) os << ",";
    os << "\"" << JsonEscape(expected.column_names[i]) << "\"";
  }
  os << "],\"rows\":[";
  for (size_t r = 0; r < expected.rows.size(); ++r) {
    if (r > 0) os << ",";
    os << "[";
    for (size_t c = 0; c < expected.rows[r].values.size(); ++c) {
      if (c > 0) os << ",";
      os << "\"" << JsonEscape(expected.rows[r].values[c]) << "\"";
    }
    os << "]";
  }
  os << "]}";
  return os.str();
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("failed to read: " + path.string());
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

int ParseManifestIntField(const std::string& json, const std::string& field) {
  const std::string needle = "\"" + field + "\"";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return 0;
  size_t i = pos + needle.size();
  SkipWs(json, i);
  if (i >= json.size() || json[i] != ':') return 0;
  ++i;
  return static_cast<int>(ParseJsonNumber(json, i));
}

std::string ParseManifestStringField(const std::string& json, const std::string& field) {
  const std::string needle = "\"" + field + "\"";
  const size_t pos = json.find(needle);
  if (pos == std::string::npos) return "";
  size_t i = pos + needle.size();
  SkipWs(json, i);
  if (i >= json.size() || json[i] != ':') return "";
  ++i;
  SkipWs(json, i);
  return ParseJsonString(json, i);
}

std::vector<std::string> ListShardFileNames(const std::filesystem::path& corpus_root) {
  std::vector<std::string> shards;
  if (!std::filesystem::exists(corpus_root)) return shards;
  for (const auto& entry : std::filesystem::directory_iterator(corpus_root)) {
    if (!entry.is_regular_file()) continue;
    const auto name = entry.path().filename().string();
    if (name.starts_with("shard_") && name.ends_with(".jsonl")) {
      shards.push_back(name);
    }
  }
  std::sort(shards.begin(), shards.end());
  return shards;
}

}  // namespace

std::filesystem::path CorpusV1Root() {
  if (auto test_srcdir = std::getenv("TEST_SRCDIR")) {
    const std::filesystem::path base(test_srcdir);
    const std::filesystem::path candidate = base / "_main" / "tests" / "generator" / "corpus" / "v1";
    if (std::filesystem::exists(candidate)) return candidate;
    const std::filesystem::path alt = base / "tests" / "generator" / "corpus" / "v1";
    if (std::filesystem::exists(alt)) return alt;
  }
  if (auto root = FindRepoRoot(std::filesystem::current_path())) {
    return *root / "tests" / "generator" / "corpus" / "v1";
  }
  return std::filesystem::current_path() / "tests" / "generator" / "corpus" / "v1";
}

std::filesystem::path CorpusManifestPath() { return CorpusV1Root() / "manifest.json"; }

CorpusManifest LoadManifest(const std::filesystem::path& manifest_path) {
  const std::string json = ReadFile(manifest_path);
  CorpusManifest manifest;
  manifest.corpus_version = ParseManifestIntField(json, "corpus_version");
  manifest.generator_fingerprint = ParseManifestStringField(json, "generator_fingerprint");
  manifest.edb_map = ParseManifestStringField(json, "edb_map");
  manifest.case_count = static_cast<size_t>(ParseManifestIntField(json, "case_count"));
  manifest.shards = ListShardFileNames(manifest_path.parent_path());
  return manifest;
}

std::vector<std::string> ListShardPaths(const CorpusManifest& manifest, const std::filesystem::path& corpus_root) {
  std::vector<std::string> paths;
  paths.reserve(manifest.shards.size());
  for (const auto& shard : manifest.shards) {
    paths.push_back((corpus_root / shard).string());
  }
  return paths;
}

CorpusCase ParseCorpusCaseLine(const std::string& line) {
  if (line.empty()) throw std::runtime_error("empty corpus line");
  CorpusCase corpus_case;
  size_t i = 0;
  SkipWs(line, i);
  ExpectChar(line, i, '{');
  while (true) {
    SkipWs(line, i);
    if (MatchLiteral(line, i, "}")) break;
    const std::string key = ParseJsonString(line, i);
    ExpectChar(line, i, ':');
    SkipWs(line, i);
    if (key == "id")
      corpus_case.id = ParseJsonString(line, i);
    else if (key == "config")
      corpus_case.config = ParseConfig(line, i);
    else if (key == "program")
      corpus_case.program = ParseJsonString(line, i);
    else if (key == "output_def")
      corpus_case.output_def = ParseJsonString(line, i);
    else if (key == "schema")
      corpus_case.schema = ParseSchema(line, i);
    else if (key == "edb")
      corpus_case.edb = ParseEdb(line, i);
    else if (key == "expected")
      corpus_case.expected = ParseExpected(line, i);
    else
      SkipValue(line, i);
    SkipWs(line, i);
    if (MatchLiteral(line, i, ",")) continue;
    if (line[i] == '}') {
      ++i;
      break;
    }
  }
  return corpus_case;
}

std::vector<CorpusCase> LoadShard(const std::filesystem::path& shard_path) {
  std::ifstream in(shard_path);
  if (!in) throw std::runtime_error("failed to open shard: " + shard_path.string());
  std::vector<CorpusCase> cases;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    cases.push_back(ParseCorpusCaseLine(line));
  }
  return cases;
}

std::string SerializeCorpusCaseLine(const CorpusCase& corpus_case) {
  std::ostringstream os;
  os << "{\"id\":\"" << JsonEscape(corpus_case.id) << "\"";
  os << ",\"config\":{\"seed\":" << corpus_case.config.seed;
  os << ",\"program_index\":" << corpus_case.config.program_index;
  os << ",\"node_budget\":" << corpus_case.config.node_budget;
  os << ",\"profile\":" << SerializeProfile(corpus_case.config.profile);
  os << "}";
  os << ",\"program\":\"" << JsonEscape(corpus_case.program) << "\"";
  os << ",\"output_def\":\"" << JsonEscape(corpus_case.output_def) << "\"";
  os << ",\"schema\":" << SerializeSchema(corpus_case.schema);
  os << ",\"edb\":{";
  bool first_table = true;
  for (const auto& kv : corpus_case.edb) {
    if (!first_table) os << ",";
    first_table = false;
    os << "\"" << JsonEscape(kv.first) << "\":" << SerializeNumericRows(kv.second);
  }
  os << "}";
  os << ",\"expected\":" << SerializeExpected(corpus_case.expected);
  os << "}";
  return os.str();
}

DataFixture DataFixtureFromCorpus(const CorpusCase& corpus_case) {
  return DataFixture::FromRows(corpus_case.schema, corpus_case.edb);
}

std::set<std::string> LoadExistingPrograms(const std::filesystem::path& corpus_root) {
  std::set<std::string> programs;
  if (!std::filesystem::exists(corpus_root)) return programs;
  for (const auto& entry : std::filesystem::directory_iterator(corpus_root)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".jsonl") continue;
    const auto name = entry.path().filename().string();
    if (!name.starts_with("shard_")) continue;
    std::ifstream in(entry.path());
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      try {
        programs.insert(ParseCorpusCaseLine(line).program);
      } catch (...) {
      }
    }
  }
  return programs;
}

}  // namespace rel2sql::generator
