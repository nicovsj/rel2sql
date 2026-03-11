#include "rel_ast/domain.h"
#include "rel_ast/rel_ast.h"

#include <algorithm>
#include <functional>
#include <sstream>

#include "support/utils.h"

namespace rel2sql {

namespace {

std::string ConstantToString(const sql::ast::constant_t& c) {
  return std::visit(
      [](auto&& x) -> std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return "\"" + x + "\"";
        } else if constexpr (std::is_same_v<T, bool>) {
          return x ? "true" : "false";
        } else {
          return std::to_string(x);
        }
      },
      c);
}

const char* DomainOperationOpToString(RelTermOp op) {
  switch (op) {
    case RelTermOp::ADD:
      return "+";
    case RelTermOp::SUB:
      return "-";
    case RelTermOp::MUL:
      return "*";
    case RelTermOp::DIV:
      return "/";
  }
  return "?";
}

}  // namespace

// ConstantSource methods
bool ConstantDomain::operator==(const Domain& other) const {
  auto other_constant = dynamic_cast<const ConstantDomain*>(&other);
  if (!other_constant) return false;
  return value == other_constant->value;
}

size_t IntensionalDomain::Arity() const {
  assert(std::dynamic_pointer_cast<const RelExpr>(node) && "node must be a shared_ptr<RelExpr>");
  return node->arity;
}

bool IntensionalDomain::operator==(const Domain& other) const {
  auto other_intensional = dynamic_cast<const IntensionalDomain*>(&other);
  if (!other_intensional) return false;
  return node == other_intensional->node;
}

std::unique_ptr<Domain> IntensionalDomain::Clone() const {
  return std::make_unique<IntensionalDomain>(node);
}

// TableSource methods
bool DefinedDomain::operator==(const Domain& other) const {
  auto other_table = dynamic_cast<const DefinedDomain*>(&other);
  if (!other_table) return false;
  return table_name == other_table->table_name;
}

Projection Projection::WithRemovedProjectionIndices(const std::vector<size_t>& indices) const {
  std::vector<size_t> new_indices = projected_indices;
  for (auto index : indices) {
    new_indices.erase(std::remove(new_indices.begin(), new_indices.end(), index), new_indices.end());
  }
  return Projection(new_indices, domain->Clone());
}

bool Projection::operator==(const Projection& other) const {
  return projected_indices == other.projected_indices && *domain == *other.domain;
}

std::size_t ConstantDomain::Hash() const {
  return std::hash<sql::ast::constant_t>()(value);
}

std::string ConstantDomain::ToString() const {
  return ConstantToString(value);
}

std::size_t DefinedDomain::Hash() const {
  return std::hash<std::string>()(table_name);
}

std::string DefinedDomain::ToString() const {
  return table_name;
}

std::size_t IntensionalDomain::Hash() const {
  return std::hash<decltype(node)>()(node);
}

std::string IntensionalDomain::ToString() const {
  return node ? node->ToString() : "?";
}

bool DomainUnion::operator==(const Domain& other) const {
  auto other_union = dynamic_cast<const DomainUnion*>(&other);
  if (!other_union) return false;
  return *lhs == *other_union->lhs && *rhs == *other_union->rhs;
}

std::size_t DomainUnion::Hash() const {
  std::size_t seed = lhs->Hash();
  return utl::hash_combine(seed, rhs->Hash());
}

std::string DomainUnion::ToString() const {
  std::ostringstream oss;
  oss << "(" << lhs->ToString() << " | " << rhs->ToString() << ")";
  return oss.str();
}

bool DomainOperation::operator==(const Domain& other) const {
  auto other_operation = dynamic_cast<const DomainOperation*>(&other);
  if (!other_operation) return false;
  return *lhs == *other_operation->lhs && *rhs == *other_operation->rhs && op == other_operation->op;
}

std::size_t DomainOperation::Hash() const {
  std::size_t seed = utl::hash_combine(0, static_cast<std::size_t>(op));
  seed = utl::hash_combine(seed, lhs->Hash());
  return utl::hash_combine(seed, rhs->Hash());
}

std::string DomainOperation::ToString() const {
  std::ostringstream oss;
  oss << "(" << lhs->ToString() << " " << DomainOperationOpToString(op) << " " << rhs->ToString()
      << ")";
  return oss.str();
}

std::size_t Projection::Hash() const {
  std::size_t seed = utl::hash_range(0, projected_indices.begin(), projected_indices.end());
  if (domain) {
    seed = utl::hash_combine(seed, domain->Hash());
  }
  return seed;
}

std::string Projection::ToString() const {
  std::ostringstream oss;
  oss << "π[";
  for (size_t i = 0; i < projected_indices.size(); ++i) {
    if (i > 0) oss << ",";
    oss << projected_indices[i];
  }
  oss << "](" << (domain ? domain->ToString() : "?") << ")";
  return oss.str();
}

}  // namespace rel2sql
