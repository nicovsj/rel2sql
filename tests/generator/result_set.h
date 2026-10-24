#ifndef REL2SQL_TESTS_GENERATOR_RESULT_SET_H_
#define REL2SQL_TESTS_GENERATOR_RESULT_SET_H_

#include <string>
#include <vector>

namespace rel2sql::generator {

struct Row {
  std::vector<std::string> values;

  bool operator==(const Row& other) const;
};

struct ResultSet {
  std::vector<std::string> column_names;
  std::vector<Row> rows;

  bool operator==(const ResultSet& other) const;
};

bool NearlyEqualDoubles(const std::string& lhs, const std::string& rhs, double epsilon = 1e-9);

ResultSet Canonicalize(ResultSet in);

bool IsEmpty(const ResultSet& rs);

// Log when a result has no rows (comparison may be inconclusive).
void WarnIfEmptyResult(std::ostream& os, const std::string& label, const ResultSet& rs);

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_RESULT_SET_H_
