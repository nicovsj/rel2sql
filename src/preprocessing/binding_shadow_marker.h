#ifndef PREPROCESSING_BINDING_SHADOW_MARKER_H
#define PREPROCESSING_BINDING_SHADOW_MARKER_H

#include <memory>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_context_builder.h"

namespace rel2sql {

// Sets RelIDTerm::shadows_relation on every id that names a known relation but is also bound as a
// variable by an enclosing binder (abstraction or quantifier), e.g. "supplier" in
// `sum[[supplier]: ps_supplycost[part, supplier]]` where "supplier" is also an EDB.
// Run it after the IDs are known and before variable analysis.
void MarkBindingShadowedIds(const std::shared_ptr<RelNode>& root, const RelContextBuilder& builder);

}  // namespace rel2sql

#endif  // PREPROCESSING_BINDING_SHADOW_MARKER_H
