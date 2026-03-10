#ifndef BINDING_SOURCE_H
#define BINDING_SOURCE_H

#include <memory>
#include <string>
#include <vector>

#include "sql_ast/sql_ast.h"

namespace rel2sql {

class RelExpr;

// Base class representing a domain of bounds.
struct Domain {
  virtual ~Domain() = default;

  // Returns the arity (number of attributes) of this source.
  virtual size_t Arity() const = 0;

  // Checks if this source is equal to another source.
  virtual bool operator==(const Domain& other) const = 0;

  // Returns a deep copy of this domain.
  virtual std::unique_ptr<Domain> Clone() const = 0;

  // Needed for hashing a Bound
  virtual std::size_t Hash() const = 0;

  virtual std::string ToString() const = 0;
};

// Represents a constant value domain (always has arity 1).
struct ConstantDomain : public Domain {
  sql::ast::constant_t value;

  ConstantDomain(sql::ast::constant_t value) : value(value) {}

  // Always returns 1 for constant sources.
  size_t Arity() const override { return 1; }

  bool operator==(const Domain& other) const override;

  std::unique_ptr<Domain> Clone() const override { return std::make_unique<ConstantDomain>(value); }

  std::size_t Hash() const override;

  std::string ToString() const override;
};

struct IntensionalDomain : public Domain {
  std::shared_ptr<RelExpr> node;

  IntensionalDomain(std::shared_ptr<RelExpr> node) : node(node) {}

  size_t Arity() const override;

  bool operator==(const Domain& other) const override;

  std::unique_ptr<Domain> Clone() const override;

  std::size_t Hash() const override;

  std::string ToString() const override;
};

// Represents a domain defined by an EDB or IDB.
struct DefinedDomain : public Domain {
  std::string table_name;
  size_t table_arity;

  DefinedDomain(std::string table_name, size_t table_arity) : table_name(table_name), table_arity(table_arity) {}

  // Returns the arity (number of columns) of this table.
  size_t Arity() const override { return table_arity; }

  // Checks if this table source equals another (by table name).
  bool operator==(const Domain& other) const override;

  std::unique_ptr<Domain> Clone() const override { return std::make_unique<DefinedDomain>(table_name, table_arity); }

  std::size_t Hash() const override;

  std::string ToString() const override;
};

// Represents a union of two domains.
struct DomainUnion : public Domain {
  std::unique_ptr<Domain> lhs;
  std::unique_ptr<Domain> rhs;

  DomainUnion(std::unique_ptr<Domain> lhs, std::unique_ptr<Domain> rhs) : lhs(std::move(lhs)), rhs(std::move(rhs)) {}

  size_t Arity() const override { return lhs->Arity(); }

  bool operator==(const Domain& other) const override;

  std::unique_ptr<Domain> Clone() const override { return std::make_unique<DomainUnion>(lhs->Clone(), rhs->Clone()); }

  std::size_t Hash() const override;

  std::string ToString() const override;
};

// Represents a operation on two domains.
struct DomainOperation : public Domain {
  // Represents the operation to be performed on the left and right sources.
  enum class Operation { ADD, SUBTRACT, MULTIPLY, DIVIDE };

  std::unique_ptr<Domain> lhs;
  std::unique_ptr<Domain> rhs;
  Operation op;

  DomainOperation(std::unique_ptr<Domain> lhs, std::unique_ptr<Domain> rhs, Operation op)
      : lhs(std::move(lhs)), rhs(std::move(rhs)), op(op) {}

  size_t Arity() const override { return lhs->Arity(); }

  bool operator==(const Domain& other) const override;

  std::unique_ptr<Domain> Clone() const override {
    return std::make_unique<DomainOperation>(lhs->Clone(), rhs->Clone(), op);
  }

  std::size_t Hash() const override;

  std::string ToString() const override;
};

// Represents a projection of a source, specifying which attributes are projected.
// The projected attributes are defined by the indices vector.
struct Projection : public Domain {
  std::vector<size_t> projected_indices;
  std::unique_ptr<Domain> domain;

  Projection(std::vector<size_t> projection_indices, std::unique_ptr<Domain> domain)
      : projected_indices(projection_indices), domain(std::move(domain)) {}

  // Returns the number of projected attributes (size of projected_indices).
  size_t Arity() const override { return projected_indices.size(); }

  bool operator==(const Domain& other) const override {
    auto* p = dynamic_cast<const Projection*>(&other);
    return p && *this == *p;
  }
  bool operator==(const Projection& other) const;

  // Returns true if no attributes are projected.
  bool IsEmpty() const { return projected_indices.empty(); }

  // True if projection projects all attributes.
  bool IsTrivial() const { return projected_indices.size() == domain->Arity(); }

  // Returns a copy of this projection with the specified indices removed.
  Projection WithRemovedProjectionIndices(const std::vector<size_t>& indices) const;

  std::unique_ptr<Domain> Clone() const override {
    return std::make_unique<Projection>(projected_indices, domain ? domain->Clone() : nullptr);
  }

  std::size_t Hash() const override;

  std::string ToString() const override;
};

}  // namespace rel2sql

#endif  // BINDING_SOURCE_H
