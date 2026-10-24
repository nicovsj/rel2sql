#include "generator/result_set.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace rel2sql::generator {
namespace {

bool TryParseDouble(const std::string& text, double* out) {
  if (text.empty()) return false;
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0') return false;
  *out = value;
  return true;
}

}  // namespace

bool NearlyEqualDoubles(const std::string& lhs, const std::string& rhs, double epsilon) {
  if (lhs == rhs) return true;
  double l = 0.0;
  double r = 0.0;
  if (!TryParseDouble(lhs, &l) || !TryParseDouble(rhs, &r)) {
    return false;
  }
  return std::fabs(l - r) <= epsilon;
}

bool Row::operator==(const Row& other) const {
  if (values.size() != other.values.size()) return false;
  for (size_t i = 0; i < values.size(); ++i) {
    if (!NearlyEqualDoubles(values[i], other.values[i])) return false;
  }
  return true;
}

bool ResultSet::operator==(const ResultSet& other) const {
  return column_names == other.column_names && rows == other.rows;
}

ResultSet Canonicalize(ResultSet in) {
  std::sort(in.rows.begin(), in.rows.end(), [](const Row& a, const Row& b) { return a.values < b.values; });
  return in;
}

bool IsEmpty(const ResultSet& rs) { return rs.rows.empty(); }

void WarnIfEmptyResult(std::ostream& os, const std::string& label, const ResultSet& rs) {
  if (!IsEmpty(rs)) return;
  os << "WARNING: " << label << " returned 0 rows (";
  for (size_t i = 0; i < rs.column_names.size(); ++i) {
    if (i > 0) os << ", ";
    os << rs.column_names[i];
  }
  os << ") — comparison may be inconclusive\n";
}

}  // namespace rel2sql::generator
