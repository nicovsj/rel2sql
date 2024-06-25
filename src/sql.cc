#include "sql.h"

namespace sql::ast {

std::ostream& Exists::Print(std::ostream& os) const { return os << "EXISTS (" << *select << ")"; }

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

}  // namespace sql::ast
