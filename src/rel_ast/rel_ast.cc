#include "rel_ast/rel_ast.h"

#include <sstream>

#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

namespace {

std::string LiteralValueToString(const RelLiteralValue& v) {
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
      v);
}

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

}  // namespace

std::string RelLiteralBinding::ToString() const {
  return LiteralValueToString(value);
}

std::string RelVarBinding::ToString() const {
  std::string out = id;
  if (domain) out += " in " + *domain;
  return out;
}

std::string RelIDApplBase::ToString() const { return id; }

std::string RelAbstractionApplBase::ToString() const {
  return rel_abs ? rel_abs->ToString() : "{}";
}

std::string RelExprApplParam::ToString() const {
  return expr ? expr->ToString() : "";
}

namespace {

std::string CompOpToString(RelCompOp op) {
  switch (op) {
    case RelCompOp::EQ: return "=";
    case RelCompOp::NEQ: return "!=";
    case RelCompOp::LT: return "<";
    case RelCompOp::GT: return ">";
    case RelCompOp::LTE: return "<=";
    case RelCompOp::GTE: return ">=";
  }
  return "?";
}

std::string LogicalOpToString(RelLogicalOp op) {
  return op == RelLogicalOp::AND ? "and" : "or";
}

std::string QuantOpToString(RelQuantOp op) {
  return op == RelQuantOp::EXISTS ? "exists" : "forall";
}

std::string TermOpToString(RelTermOp op) {
  switch (op) {
    case RelTermOp::ADD: return "+";
    case RelTermOp::SUB: return "-";
    case RelTermOp::MUL: return "*";
    case RelTermOp::DIV: return "/";
  }
  return "?";
}

}  // namespace

void RelLiteral::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }

std::string RelLiteral::ToString() const { return LiteralValueToString(value); }

void RelAbstraction::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelAbstraction::ToString() const {
  std::ostringstream out;
  out << "{";
  for (size_t i = 0; i < exprs.size(); ++i) {
    if (i) out << "; ";
    if (exprs[i]) out << exprs[i]->ToString();
  }
  out << "}";
  return out.str();
}
void RelIDTerm::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelIDTerm::ToStringImpl() const { return id; }

void RelNumTerm::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelNumTerm::ToStringImpl() const { return ConstantToString(value); }

void RelOpTerm::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelOpTerm::ToStringImpl() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " " + TermOpToString(op) + " " + r;
}
void RelParenthesisTerm::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelParenthesisTerm::ToStringImpl() const {
  return term ? "(" + term->ToString() + ")" : "()";
}
void RelFormulaBool::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }

std::string RelFormulaBool::ToStringImpl() const { return "true"; }

void RelComparison::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }

std::string RelComparison::ToStringImpl() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " " + CompOpToString(op) + " " + r;
}
void RelUnOp::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelUnOp::ToStringImpl() const {
  return formula ? "not " + formula->ToString() : "not (?)";
}
void RelBinOp::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelBinOp::ToStringImpl() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " " + LogicalOpToString(op) + " " + r;
}

void RelParen::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }

std::string RelParen::ToStringImpl() const {
  return formula ? "(" + formula->ToString() + ")" : "()";
}
void RelQuantification::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelQuantification::ToStringImpl() const {
  std::ostringstream out;
  out << QuantOpToString(op) << "( (";
  for (size_t i = 0; i < bindings.size(); ++i) {
    if (i) out << ", ";
    out << (bindings[i] ? bindings[i]->ToString() : "?");
  }
  out << ") | " << (formula ? formula->ToString() : "?");
  out << ")";
  return out.str();
}
void RelFullAppl::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelFullAppl::ToStringImpl() const {
  std::ostringstream out;
  out << (base ? base->ToString() : "?");
  if (!params.empty()) {
    out << "(";
    for (size_t i = 0; i < params.size(); ++i) {
      if (i) out << ", ";
      out << (params[i] ? params[i]->ToString() : "?");
    }
    out << ")";
  }
  return out.str();
}
void RelLitExpr::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelLitExpr::ToStringImpl() const {
  return literal ? literal->ToString() : "?";
}
void RelTermExpr::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelTermExpr::ToStringImpl() const {
  return term ? term->ToString() : "?";
}
void RelProductExpr::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelProductExpr::ToStringImpl() const {
  std::ostringstream out;
  for (size_t i = 0; i < exprs.size(); ++i) {
    if (i) out << ", ";
    if (exprs[i]) out << exprs[i]->ToString();
  }
  return out.str();
}
void RelConditionExpr::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelConditionExpr::ToStringImpl() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " where " + r;
}
void RelAbstractionExpr::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelAbstractionExpr::ToStringImpl() const {
  return rel_abs ? rel_abs->ToString() : "{}";
}
void RelFormulaExpr::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelFormulaExpr::ToStringImpl() const {
  return formula ? formula->ToString() : "?";
}
void RelBindingsExpr::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelBindingsExpr::ToStringImpl() const {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < bindings.size(); ++i) {
    if (i) out << ", ";
    out << (bindings[i] ? bindings[i]->ToString() : "?");
  }
  out << "] : ";
  out << (expr ? expr->ToString() : "?");
  return out.str();
}
void RelBindingsFormula::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelBindingsFormula::ToStringImpl() const {
  std::ostringstream out;
  out << "(";
  for (size_t i = 0; i < bindings.size(); ++i) {
    if (i) out << ", ";
    out << (bindings[i] ? bindings[i]->ToString() : "?");
  }
  out << ")";
  out << " : " << (formula ? formula->ToString() : "?");
  return out.str();
}
void RelPartialAppl::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelPartialAppl::ToStringImpl() const {
  std::ostringstream out;
  out << (base ? base->ToString() : "?");
  if (!params.empty()) {
    out << "[";
    for (size_t i = 0; i < params.size(); ++i) {
      if (i) out << ", ";
      out << (params[i] ? params[i]->ToString() : "?");
    }
    out << "]";
  }
  return out.str();
}
void RelDef::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelDef::ToString() const {
  return "def " + name + " " + (body ? body->ToString() : "{}");
}
void RelProgram::Accept(RelASTVisitor& visitor) { visitor.Visit(*this); }
std::string RelProgram::ToString() const {
  std::ostringstream out;
  for (size_t i = 0; i < defs.size(); ++i) {
    if (i) out << "\n";
    if (defs[i]) out << defs[i]->ToString();
  }
  return out.str();
}

}  // namespace rel2sql
