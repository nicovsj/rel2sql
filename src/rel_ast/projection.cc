#include "rel_ast/projection.h"

#include <algorithm>

namespace rel2sql {

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
