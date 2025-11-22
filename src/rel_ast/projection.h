#ifndef BINDING_SOURCE_H
#define BINDING_SOURCE_H

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "sql_ast/sql_ast.h"
#include "support/utils.h"

namespace rel2sql {

// Base class representing a source of bindings (e.g., a table or constant value).
struct BoundSource {
  virtual ~BoundSource() = default;

  // Returns the arity (number of attributes) of this source.
  virtual int Arity() const = 0;

  // Checks if this source is equal to another source.
  virtual bool operator==(const BoundSource& other) const = 0;
};

// Represents a constant value source (always has arity 1).
struct ConstantSource : public BoundSource {
  sql::ast::constant_t value;

  ConstantSource(sql::ast::constant_t value) : value(value) {}

  // Always returns 1 for constant sources.
  int Arity() const override { return 1; }

  // Checks if this constant source equals another (by value).
  bool operator==(const BoundSource& other) const override;
};

// Represents a table source with a given name and arity.
struct TableSource : public BoundSource {
  std::string table_name;
  int table_arity;

  TableSource(std::string table_name, int table_arity) : table_name(table_name), table_arity(table_arity) {}

  // Returns the arity (number of columns) of this table.
  int Arity() const override { return table_arity; }

  // Checks if this table source equals another (by table name).
  bool operator==(const BoundSource& other) const override;
};

// Represents a projection of a source, specifying which attributes are projected.
// The projected attributes are defined by the indices vector.
struct Projection {
  std::vector<size_t> projected_indices;
  std::shared_ptr<BoundSource> source;

  Projection(std::vector<size_t> projection_indices, std::shared_ptr<BoundSource> source)
      : projected_indices(projection_indices), source(source) {}

  // Creates a projection that projects all attributes of the given table.
  Projection(TableSource source);

  // Creates a projection for a constant source (projects index 0).
  Projection(ConstantSource source);

  // Returns the number of projected attributes (size of projected_indices).
  int Arity() const { return projected_indices.size(); }

  // Returns true if no attributes are projected.
  bool IsEmpty() const { return projected_indices.empty(); }

  // Returns a copy of this projection with the specified indices removed.
  Projection WithRemovedProjectionIndices(const std::vector<size_t>& indices) const;

  // Checks if this projection equals another (by indices and source).
  bool operator==(const Projection& other) const;
};

}  // namespace rel2sql

namespace std {

template <>
struct hash<rel2sql::TableSource> {
  std::size_t operator()(const rel2sql::TableSource& ts) const { return std::hash<std::string>()(ts.table_name); }
};

template <>
struct hash<rel2sql::ConstantSource> {
  std::size_t operator()(const rel2sql::ConstantSource& cs) const {
    return std::hash<rel2sql::sql::ast::constant_t>()(cs.value);
  }
};

template <>
struct hash<rel2sql::Projection> {
  std::size_t operator()(const rel2sql::Projection& pt) const {
    std::size_t seed = 0;
    utl::hash_range(seed, pt.projected_indices.begin(), pt.projected_indices.end());
    if (auto table_source = dynamic_cast<const rel2sql::TableSource*>(pt.source.get())) {
      utl::hash_combine(seed, *table_source);
    } else if (auto constant_source = dynamic_cast<const rel2sql::ConstantSource*>(pt.source.get())) {
      utl::hash_combine(seed, *constant_source);
    } else {
      throw std::runtime_error("Unknown projection source type");
    }
    return seed;
  }
};

// Intentionally no std::hash<rel2sql::BindingSource> specialization

}  // namespace std

#endif  // BINDING_SOURCE_H
