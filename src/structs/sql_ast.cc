#include "sql_ast.h"

namespace rel2sql {

namespace sql::ast {

std::string Expression::ToString() const {
  std::stringstream ss;
  Print(ss);
  return ss.str();
}

std::ostream& Exists::Print(std::ostream& os) const { return os << "EXISTS (" << *select << ")"; }

bool Exists::Equals(const Expression& other) const {
  const auto* other_exists = dynamic_cast<const Exists*>(&other);
  if (!other_exists) return false;
  return *select == *other_exists->select;
}

std::ostream& Inclusion::Print(std::ostream& os) const {
  if (columns.size() == 1) {
    os << *columns.at(0);
  } else {
    os << "(" << *columns.at(0);
    for (size_t i = 1; i < columns.size(); i++) {
      os << ", " << *columns.at(i);
    }
    os << ")";
  }

  if (is_not) {
    os << " NOT";
  }

  return os << " IN (" << *select << ")";
}

bool Inclusion::Equals(const Expression& other) const {
  const auto* other_inclusion = dynamic_cast<const Inclusion*>(&other);
  if (!other_inclusion) return false;
  if (is_not != other_inclusion->is_not || columns.size() != other_inclusion->columns.size()) return false;
  for (size_t i = 0; i < columns.size(); i++) {
    if (*columns[i] != *other_inclusion->columns[i]) return false;
  }
  return *select == *other_inclusion->select;
}

}  // namespace sql::ast
}  // namespace rel2sql
