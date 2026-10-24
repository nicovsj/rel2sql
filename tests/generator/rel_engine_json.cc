#include "generator/rel_engine_json.h"

#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

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
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
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
  if (i >= s.size() || s[i] != expected) {
    throw std::runtime_error("JSON parse error");
  }
  ++i;
}

std::vector<std::string> ParseStringArray(const std::string& s, size_t& i) {
  std::vector<std::string> out;
  ExpectChar(s, i, '[');
  SkipWs(s, i);
  if (MatchLiteral(s, i, "]")) return out;
  while (true) {
    out.push_back(ParseJsonString(s, i));
    SkipWs(s, i);
    if (MatchLiteral(s, i, "]")) break;
    ExpectChar(s, i, ',');
  }
  return out;
}

std::vector<std::vector<std::string>> ParseRowsArray(const std::string& s, size_t& i) {
  std::vector<std::vector<std::string>> rows;
  ExpectChar(s, i, '[');
  SkipWs(s, i);
  if (MatchLiteral(s, i, "]")) return rows;
  while (true) {
    rows.push_back(ParseStringArray(s, i));
    SkipWs(s, i);
    if (MatchLiteral(s, i, "]")) break;
    ExpectChar(s, i, ',');
  }
  return rows;
}

std::string FindJsonStringField(const std::string& s, const std::string& field) {
  const std::string needle = "\"" + field + "\"";
  const size_t pos = s.find(needle);
  if (pos == std::string::npos) return "";
  size_t i = pos + needle.size();
  SkipWs(s, i);
  if (i >= s.size() || s[i] != ':') return "";
  ++i;
  SkipWs(s, i);
  return ParseJsonString(s, i);
}

}  // namespace

std::string JsonEscape(std::string_view text) {
  std::ostringstream os;
  for (char c : text) {
    switch (c) {
      case '"':
        os << "\\\"";
        break;
      case '\\':
        os << "\\\\";
        break;
      case '\n':
        os << "\\n";
        break;
      case '\r':
        os << "\\r";
        break;
      case '\t':
        os << "\\t";
        break;
      default:
        os << c;
        break;
    }
  }
  return os.str();
}

std::string BuildRelEngineRequestJson(const std::string& program, const std::string& output_def,
                                      const DataFixture& fixture, int output_arity) {
  std::ostringstream os;
  os << "{\n";
  os << "  \"program\": \"" << JsonEscape(program) << "\",\n";
  os << "  \"output\": \"" << JsonEscape(output_def) << "\",\n";
  os << "  \"output_arity\": " << output_arity << ",\n";
  os << "  \"edb\": {\n";

  bool first_table = true;
  for (const auto& kv : fixture.Rows()) {
    if (!first_table) os << ",\n";
    first_table = false;
    os << "    \"" << JsonEscape(kv.first) << "\": [";
    bool first_row = true;
    for (const auto& row : kv.second) {
      if (!first_row) os << ", ";
      first_row = false;
      os << "[";
      for (size_t c = 0; c < row.size(); ++c) {
        if (c > 0) os << ", ";
        os << row[c];
      }
      os << "]";
    }
    os << "]";
  }

  os << "\n  }\n";
  os << "}\n";
  return os.str();
}

std::string BuildRelEngineRunRequestLine(const std::string& program, const std::string& output_def,
                                         const DataFixture& fixture, int output_arity) {
  std::ostringstream os;
  os << "{\"op\":\"run\",\"program\":\"" << JsonEscape(program) << "\",\"output\":\"" << JsonEscape(output_def)
     << "\",\"output_arity\":" << output_arity << ",\"edb\":{";

  bool first_table = true;
  for (const auto& kv : fixture.Rows()) {
    if (!first_table) os << ",";
    first_table = false;
    os << "\"" << JsonEscape(kv.first) << "\":[";
    bool first_row = true;
    for (const auto& row : kv.second) {
      if (!first_row) os << ",";
      first_row = false;
      os << "[";
      for (size_t c = 0; c < row.size(); ++c) {
        if (c > 0) os << ",";
        os << row[c];
      }
      os << "]";
    }
    os << "]";
  }

  os << "}}";
  return os.str();
}

ResultSet ParseRelEngineResponseJson(const std::string& json) {
  const std::string error = FindJsonStringField(json, "error");
  if (!error.empty()) {
    throw std::runtime_error("RAICode runner error: " + error);
  }

  ResultSet out;
  size_t i = 0;
  SkipWs(json, i);
  ExpectChar(json, i, '{');

  while (true) {
    SkipWs(json, i);
    if (MatchLiteral(json, i, "}")) break;
    const std::string key = ParseJsonString(json, i);
    ExpectChar(json, i, ':');
    SkipWs(json, i);
    if (key == "columns") {
      out.column_names = ParseStringArray(json, i);
    } else if (key == "rows") {
      const auto row_strings = ParseRowsArray(json, i);
      out.rows.reserve(row_strings.size());
      for (const auto& values : row_strings) {
        Row row;
        row.values = values;
        out.rows.push_back(std::move(row));
      }
    } else {
      // Skip unknown field values at top level (string, array, object)
      if (json[i] == '"') {
        (void)ParseJsonString(json, i);
      } else if (json[i] == '[') {
        (void)ParseRowsArray(json, i);
      } else if (json[i] == '{') {
        int depth = 0;
        while (i < json.size()) {
          if (json[i] == '{') ++depth;
          if (json[i] == '}') {
            --depth;
            ++i;
            if (depth == 0) break;
          } else {
            ++i;
          }
        }
      }
    }
    SkipWs(json, i);
    if (MatchLiteral(json, i, ",")) continue;
    if (json[i] == '}') {
      ++i;
      break;
    }
  }

  return Canonicalize(std::move(out));
}

}  // namespace rel2sql::generator
