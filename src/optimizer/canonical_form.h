#ifndef CANONICAL_FORM_H
#define CANONICAL_FORM_H

#include <memory>
#include <optional>

#include "sql_ast/sql_ast.h"

namespace rel2sql {
namespace sql::ast {

// CanonicalForm is defined in sql_ast.h (uses Term).

// Computes canonical form for a term (e.g. 22*(T2.A1+3)/22 + -3 -> constant=0, terms=[(T2.A1, 1)]).
std::optional<CanonicalForm> ComputeCanonicalForm(const std::shared_ptr<Term>& term);

// Returns true if two canonical forms are algebraically equal.
bool CanonicalFormsEqual(const CanonicalForm& a, const CanonicalForm& b);

// Returns true if lhs and rhs of an equality condition are algebraically equal.
// Uses stored lhs_canonical/rhs_canonical when present, else computes on the fly.
bool AreEqualityExpressionsEqual(const std::shared_ptr<ComparisonCondition>& comp);

// Returns true if the equality is a tautology: both sides are algebraically equal and
// reduce to the same single column (e.g. T0.A1 = 2 * T0.A1 / 2). Used to remove
// redundant conditions after self-join elimination.
bool IsTautologyByCanonicalForm(const std::shared_ptr<ComparisonCondition>& comp);

}  // namespace sql::ast
}  // namespace rel2sql

#endif  // CANONICAL_FORM_H
