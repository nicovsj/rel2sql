#ifndef TUPLE_BINDING_H
#define TUPLE_BINDING_H

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "sql_ast/sql_ast.h"
#include "support/utils.h"

namespace rel2sql {

struct BoundSource {
  virtual ~BoundSource() = default;

  virtual int Arity() const = 0;

  virtual bool operator==(const BoundSource& other) const = 0;
};

struct ConstantSource : public BoundSource {
  sql::ast::constant_t value;

  ConstantSource(sql::ast::constant_t value) : value(value) {}

  int Arity() const override { return 1; }

  bool operator==(const BoundSource& other) const override {
    auto other_constant = dynamic_cast<const ConstantSource*>(&other);
    if (!other_constant) return false;
    return value == other_constant->value;
  }
};

struct TableSource : public BoundSource {
  std::string table_name;
  int table_arity;

  TableSource(std::string table_name, int table_arity) : table_name(table_name), table_arity(table_arity) {}

  int Arity() const override { return table_arity; }

  bool operator==(const BoundSource& other) const override {
    auto other_table = dynamic_cast<const TableSource*>(&other);
    if (!other_table) return false;
    return table_name == other_table->table_name;
  }
};

// Represents a projection of a source. The projected attributes are defined by the indices vector.
struct SourceProjection {
  std::vector<size_t> projected_indices;
  std::shared_ptr<BoundSource> source;

  SourceProjection(std::vector<size_t> projection_indices, std::shared_ptr<BoundSource> source)
      : projected_indices(projection_indices), source(source) {}

  // Assume that giving a table source, we are projecting all the attributes of the table.
  SourceProjection(TableSource source) : source(std::make_shared<TableSource>(source)) {
    for (size_t i = 0; i < source.Arity(); i++) {
      projected_indices.push_back(i);
    }
  }

  SourceProjection(ConstantSource source) : source(std::make_shared<ConstantSource>(source)) {
    projected_indices.push_back(0);
  }

  int Arity() const { return projected_indices.size(); }

  bool IsEmpty() const { return projected_indices.empty(); }

  // Return a copy of the source projection with the given projection indices removed
  SourceProjection WithRemovedProjectionIndices(const std::vector<size_t>& indices) const {
    std::vector<size_t> new_indices = projected_indices;
    for (auto index : indices) {
      new_indices.erase(std::remove(new_indices.begin(), new_indices.end(), index), new_indices.end());
    }
    return SourceProjection(new_indices, source);
  }

  bool operator==(const SourceProjection& other) const {
    return projected_indices == other.projected_indices && *source == *other.source;
  }
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
struct hash<rel2sql::SourceProjection> {
  std::size_t operator()(const rel2sql::SourceProjection& pt) const {
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

namespace rel2sql {

struct BindingsBound {
  std::vector<std::string> variables;
  std::unordered_set<SourceProjection> domain;  // The domain of the binding bound will be a union of sources

  BindingsBound() = default;

  explicit BindingsBound(std::vector<std::string> variables) : variables(std::move(variables)) {}

  BindingsBound(std::vector<std::string> variables, std::unordered_set<SourceProjection> domain)
      : variables(std::move(variables)), domain(std::move(domain)) {}

  void Add(const SourceProjection& projection) { domain.insert(projection); }

  void Add(const ConstantSource& constant) { domain.insert(SourceProjection(constant)); }

  bool IsCorrect() const {
    for (auto& projection : domain) {
      if (projection.Arity() != variables.size()) return false;
    }
    return true;
  }

  BindingsBound WithRemovedIndices(const std::vector<size_t>& indices) const {
    std::vector<std::string> new_variables = variables;
    // Sort indices in descending order to safely erase from end to beginning
    std::vector<size_t> sorted_indices = indices;
    std::sort(sorted_indices.rbegin(), sorted_indices.rend());
    for (auto index : sorted_indices) {
      if (index < new_variables.size()) {
        new_variables.erase(new_variables.begin() + index);
      }
    }
    std::unordered_set<SourceProjection> new_domain;
    for (auto& projection : domain) {
      new_domain.insert(projection.WithRemovedProjectionIndices(indices));
    }
    return BindingsBound(new_variables, new_domain);
  }

  BindingsBound mergedWith(const BindingsBound& other) const {
    std::unordered_set<SourceProjection> merged = domain;
    merged.insert(domain.begin(), domain.end());
    merged.insert(other.domain.begin(), other.domain.end());
    return BindingsBound{variables, std::move(merged)};
  }

  bool operator==(const BindingsBound& other) const { return variables == other.variables && domain == other.domain; }
};

}  // namespace rel2sql

namespace std {

template <>
struct hash<rel2sql::BindingsBound> {
  std::size_t operator()(const rel2sql::BindingsBound& tb) const {
    std::size_t seed = 0;
    utl::hash_range(seed, tb.variables.begin(), tb.variables.end());
    utl::hash_range(seed, tb.domain.begin(), tb.domain.end());
    return seed;
  }
};

}  // namespace std

#endif  // TUPLE_BINDING_H
