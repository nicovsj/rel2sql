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

std::string CompOpToString(RelCompOp op) {
  switch (op) {
    case RelCompOp::EQ:
      return "=";
    case RelCompOp::NEQ:
      return "!=";
    case RelCompOp::LT:
      return "<";
    case RelCompOp::GT:
      return ">";
    case RelCompOp::LTE:
      return "<=";
    case RelCompOp::GTE:
      return ">=";
  }
  return "?";
}

std::string TermOpToString(RelTermOp op) {
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

std::shared_ptr<RelNode> RelLiteral::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelLiteral>(self));
}

std::string RelLiteral::ToString() const { return LiteralValueToString(value); }

std::shared_ptr<RelNode> RelAbstraction::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelAbstraction>(self));
}

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
std::shared_ptr<RelNode> RelIDTerm::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelIDTerm>(self));
}
std::string RelIDTerm::ToString() const { return id; }

std::shared_ptr<RelNode> RelNumTerm::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelNumTerm>(self));
}

std::string RelNumTerm::ToString() const { return ConstantToString(value); }

std::shared_ptr<RelNode> RelOpTerm::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelOpTerm>(self));
}

std::string RelOpTerm::ToString() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " " + TermOpToString(op) + " " + r;
}

std::shared_ptr<RelNode> RelParenthesisTerm::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelParenthesisTerm>(self));
}

std::string RelExprApplParam::ToString() const { return expr ? expr->ToString() : ""; }

std::shared_ptr<RelNode> RelExprApplParam::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelExprApplParam>(self));
}

std::shared_ptr<RelNode> RelUnderscoreParam::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelUnderscoreParam>(self));
}

std::string RelIDApplBase::ToString() const { return id; }

std::shared_ptr<RelNode> RelIDApplBase::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelIDApplBase>(self));
}

std::string RelAbstractionApplBase::ToString() const { return rel_abs ? rel_abs->ToString() : "{}"; }

std::shared_ptr<RelNode> RelAbstractionApplBase::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelAbstractionApplBase>(self));
}

std::shared_ptr<RelNode> RelLiteralBinding::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelLiteralBinding>(self));
}

std::string RelLiteralBinding::ToString() const { return LiteralValueToString(value); }

std::shared_ptr<RelNode> RelVarBinding::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelVarBinding>(self));
}

std::string RelVarBinding::ToString() const { return id; }

std::string RelParenthesisTerm::ToString() const { return term ? "(" + term->ToString() + ")" : "()"; }

std::shared_ptr<RelNode> RelFormulaBool::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelFormulaBool>(self));
}

std::string RelFormulaBool::ToString() const { return "true"; }

std::shared_ptr<RelNode> RelComparison::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelComparison>(self));
}

std::string RelComparison::ToString() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " " + CompOpToString(op) + " " + r;
}
std::shared_ptr<RelNode> RelNegation::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelNegation>(self));
}

std::string RelNegation::ToString() const { return formula ? "not " + formula->ToString() : "not (?)"; }

std::shared_ptr<RelNode> RelDisjunction::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelDisjunction>(self));
}

std::string RelDisjunction::ToString() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " or " + r;
}

std::shared_ptr<RelNode> RelConjunction::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelConjunction>(self));
}

std::string RelConjunction::ToString() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " and " + r;
}

std::shared_ptr<RelNode> RelParen::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelParen>(self));
}

std::string RelParen::ToString() const { return formula ? "(" + formula->ToString() + ")" : "()"; }

std::shared_ptr<RelNode> RelExistential::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelExistential>(self));
}
std::string RelExistential::ToString() const {
  std::ostringstream out;
  out << "exists" << "( (";
  for (size_t i = 0; i < bindings.size(); ++i) {
    if (i) out << ", ";
    out << (bindings[i] ? bindings[i]->ToString() : "?");
  }
  out << ") | " << (formula ? formula->ToString() : "?");
  out << ")";
  return out.str();
}

std::shared_ptr<RelNode> RelUniversal::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelUniversal>(self));
}
std::string RelUniversal::ToString() const {
  std::ostringstream out;
  out << "forall" << "( (";
  for (size_t i = 0; i < bindings.size(); ++i) {
    if (i) out << ", ";
    out << (bindings[i] ? bindings[i]->ToString() : "?");
  }
  out << ") | " << (formula ? formula->ToString() : "?");
  out << ")";
  return out.str();
}

std::shared_ptr<RelNode> RelFullAppl::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelFullAppl>(self));
}
std::string RelFullAppl::ToString() const {
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

std::shared_ptr<RelNode> RelProduct::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelProduct>(self));
}
std::string RelProduct::ToString() const {
  std::ostringstream out;
  out << "(";
  for (size_t i = 0; i < exprs.size(); ++i) {
    if (i) out << ", ";
    if (exprs[i]) out << exprs[i]->ToString();
  }
  out << ")";
  return out.str();
}
std::shared_ptr<RelNode> RelCondition::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelCondition>(self));
}
std::string RelCondition::ToString() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " where " + r;
}
std::shared_ptr<RelNode> RelAbstractionExpr::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelAbstractionExpr>(self));
}
std::string RelAbstractionExpr::ToString() const { return rel_abs ? rel_abs->ToString() : "{}"; }

std::shared_ptr<RelNode> RelFormulaExpr::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelFormulaExpr>(self));
}
std::string RelFormulaExpr::ToString() const { return formula ? formula->ToString() : "?"; }

std::shared_ptr<RelNode> RelBindingsExpr::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelBindingsExpr>(self));
}
std::string RelBindingsExpr::ToString() const {
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
std::shared_ptr<RelNode> RelBindingsFormula::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelBindingsFormula>(self));
}
std::string RelBindingsFormula::ToString() const {
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
std::shared_ptr<RelNode> RelPartialAppl::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelPartialAppl>(self));
}
std::string RelPartialAppl::ToString() const {
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
std::shared_ptr<RelNode> RelDef::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelDef>(self));
}
std::string RelDef::ToString() const { return "def " + name + " " + (body ? body->ToString() : "{}"); }

std::shared_ptr<RelNode> RelProgram::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelProgram>(self));
}
std::string RelProgram::ToString() const {
  std::ostringstream out;
  for (size_t i = 0; i < defs.size(); ++i) {
    if (i) out << "\n";
    if (defs[i]) out << defs[i]->ToString();
  }
  return out.str();
}

}  // namespace rel2sql
