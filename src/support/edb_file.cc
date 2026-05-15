#include "rel2sql/edb_file.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rel2sql {

namespace {

std::string Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
    s.remove_suffix(1);
  }
  return std::string(s);
}

bool AllDigits(std::string_view s) {
  if (s.empty()) {
    return false;
  }
  for (char c : s) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      return false;
    }
  }
  return true;
}

}  // namespace

RelationMap LoadEdbFile(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("LoadEdbFile: could not open \"" + path + "\"");
  }
  RelationMap out;
  std::string line;
  int line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    const auto trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }
    std::istringstream iss(trimmed);
    std::string name;
    std::string arity_str;
    if (!(iss >> name >> arity_str)) {
      throw std::runtime_error("LoadEdbFile: malformed line " + std::to_string(line_no) + " in \"" + path +
                               "\": expected \"<name> <arity>\"");
    }
    std::string extra;
    if (iss >> extra) {
      throw std::runtime_error("LoadEdbFile: extra tokens on line " + std::to_string(line_no) + " in \"" + path + "\"");
    }
    if (!AllDigits(arity_str)) {
      throw std::runtime_error("LoadEdbFile: bad arity on line " + std::to_string(line_no) + " in \"" + path + "\"");
    }
    const int arity = std::stoi(arity_str);
    if (arity <= 0) {
      throw std::runtime_error("LoadEdbFile: arity must be positive (line " + std::to_string(line_no) + ") in \"" +
                               path + "\"");
    }
    if (out.has(name)) {
      throw std::runtime_error("LoadEdbFile: duplicate relation \"" + name + "\" in \"" + path + "\"");
    }
    out[name] = RelationInfo(arity);
  }
  return out;
}

}  // namespace rel2sql
