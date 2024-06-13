#include "sql.h"

std::ostream& Subquery::Print(std::ostream& os) const { return os << "(" << *select << ") AS " << alias; }
