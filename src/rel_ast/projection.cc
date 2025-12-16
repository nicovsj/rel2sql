#include "rel_ast/projection.h"

#include <algorithm>

namespace rel2sql {

// GenericSource methods
bool GenericSource::operator==(const BoundSource& other) const {
  auto other_generic = dynamic_cast<const GenericSource*>(&other);
  if (!other_generic) return false;
  if (arity != other_generic->arity) return false;
  if (!source && !other_generic->source) return true;
  if (!source || !other_generic->source) return false;
  return *source == *other_generic->source;
}

bool PromisedSource::operator==(const BoundSource& other) const {
  if (this == &other) return true;

  const auto* other_promise = dynamic_cast<const PromisedSource*>(&other);
  if (!IsFulfilled() || (other_promise && !other_promise->IsFulfilled())) {
    // When unresolved, equality falls back to identity
    return other_promise && this == other_promise;
  }

  if (other_promise) {
    return *resolved_ == *other_promise->resolved_;
  }

  // For non-promised, attempt to resolve to a temporary BoundSource and compare
  if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(resolved_)) {
    TableSource as_table(table->name, static_cast<size_t>(table->arity));
    return as_table == other;
  }

  if (dynamic_cast<const GenericSource*>(&other)) {
    GenericSource as_generic(resolved_, Arity());
    return as_generic == other;
  }

  return false;
}

std::shared_ptr<sql::ast::Sourceable> PromisedSource::Resolve() const {
  if (!resolved_) throw std::runtime_error("PromisedSource has not been fulfilled");
  return resolved_;
}

void PromisedSource::Fulfill(std::shared_ptr<sql::ast::Sourceable> source) {
  if (!source) throw std::invalid_argument("Cannot fulfill PromisedSource with null source");
  resolved_ = std::move(source);
}

std::shared_ptr<BoundSource> ResolvePromisedSource(const std::shared_ptr<BoundSource>& source) {
  if (auto promise = std::dynamic_pointer_cast<PromisedSource>(source)) {
    if (promise->IsFulfilled()) {
      auto resolved_sql = promise->Resolve();
      if (auto table = std::dynamic_pointer_cast<sql::ast::Table>(resolved_sql)) {
        return std::make_shared<TableSource>(table->name, static_cast<size_t>(table->arity));
      }
      return std::make_shared<GenericSource>(resolved_sql, promise->Arity());
    }
    return promise;
  }
  return source;
}

// ConstantSource methods
bool ConstantSource::operator==(const BoundSource& other) const {
  auto other_constant = dynamic_cast<const ConstantSource*>(&other);
  if (!other_constant) return false;
  return value == other_constant->value;
}

// TableSource methods
bool TableSource::operator==(const BoundSource& other) const {
  auto other_table = dynamic_cast<const TableSource*>(&other);
  if (!other_table) return false;
  return table_name == other_table->table_name;
}

// SourceProjection methods
Projection::Projection(TableSource source) : source(std::make_shared<TableSource>(source)) {
  for (size_t i = 0; i < source.Arity(); i++) {
    projected_indices.push_back(i);
  }
}

Projection::Projection(ConstantSource source) : source(std::make_shared<ConstantSource>(source)) {
  projected_indices.push_back(0);
}

Projection::Projection(PromisedSource source) : source(std::make_shared<PromisedSource>(source)) {
  for (size_t i = 0; i < source.Arity(); i++) {
    projected_indices.push_back(i);
  }
}

Projection::Projection(GenericSource source) : source(std::make_shared<GenericSource>(source)) {
  for (size_t i = 0; i < source.Arity(); i++) {
    projected_indices.push_back(i);
  }
}

Projection Projection::WithRemovedProjectionIndices(const std::vector<size_t>& indices) const {
  std::vector<size_t> new_indices = projected_indices;
  for (auto index : indices) {
    new_indices.erase(std::remove(new_indices.begin(), new_indices.end(), index), new_indices.end());
  }
  return Projection(new_indices, source);
}

bool Projection::operator==(const Projection& other) const {
  return projected_indices == other.projected_indices && *source == *other.source;
}

}  // namespace rel2sql
