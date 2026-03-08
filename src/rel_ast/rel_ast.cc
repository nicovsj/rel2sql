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

std::vector<std::shared_ptr<RelNode>> RelLiteral::Children() const { return {}; }

std::shared_ptr<RelNode> RelUnion::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelUnion>(self));
}

std::string RelUnion::ToString() const {
  std::ostringstream out;
  out << "{";
  for (size_t i = 0; i < exprs.size(); ++i) {
    if (i) out << "; ";
    if (exprs[i]) out << exprs[i]->ToString();
  }
  out << "}";
  return out.str();
}

std::vector<std::shared_ptr<RelNode>> RelUnion::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  for (auto& expr : exprs) {
    if (expr) children.push_back(expr);
  }
  return children;
}

std::shared_ptr<RelNode> RelIDTerm::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelIDTerm>(self));
}
std::string RelIDTerm::ToString() const { return id; }

std::vector<std::shared_ptr<RelNode>> RelIDTerm::Children() const { return {}; }

std::shared_ptr<RelNode> RelNumTerm::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelNumTerm>(self));
}

std::string RelNumTerm::ToString() const { return ConstantToString(value); }

std::vector<std::shared_ptr<RelNode>> RelNumTerm::Children() const { return {}; }

std::shared_ptr<RelNode> RelOpTerm::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelOpTerm>(self));
}

std::string RelOpTerm::ToString() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " " + TermOpToString(op) + " " + r;
}

std::vector<std::shared_ptr<RelNode>> RelOpTerm::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (lhs) children.push_back(lhs);
  if (rhs) children.push_back(rhs);
  return children;
}

std::shared_ptr<RelNode> RelParenthesisTerm::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelParenthesisTerm>(self));
}

std::string RelExprApplParam::ToString() const { return expr ? expr->ToString() : ""; }

std::shared_ptr<RelNode> RelExprApplParam::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelExprApplParam>(self));
}

std::vector<std::shared_ptr<RelNode>> RelExprApplParam::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (expr) children.push_back(expr);
  return children;
}

std::shared_ptr<RelNode> RelWildcardParam::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelWildcardParam>(self));
}

std::vector<std::shared_ptr<RelNode>> RelWildcardParam::Children() const { return {}; }

std::string RelIDApplBase::ToString() const { return id; }

std::shared_ptr<RelNode> RelIDApplBase::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelIDApplBase>(self));
}

std::vector<std::shared_ptr<RelNode>> RelIDApplBase::Children() const { return {}; }

std::string RelExprApplBase::ToString() const { return expr ? expr->ToString() : "{}"; }

std::shared_ptr<RelNode> RelExprApplBase::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelExprApplBase>(self));
}

std::vector<std::shared_ptr<RelNode>> RelExprApplBase::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (expr) children.push_back(expr);
  return children;
}

std::shared_ptr<RelNode> RelLiteralBinding::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelLiteralBinding>(self));
}

std::string RelLiteralBinding::ToString() const { return LiteralValueToString(value); }

std::vector<std::shared_ptr<RelNode>> RelLiteralBinding::Children() const { return {}; }

std::shared_ptr<RelNode> RelVarBinding::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelVarBinding>(self));
}

std::string RelVarBinding::ToString() const { return id; }

std::vector<std::shared_ptr<RelNode>> RelVarBinding::Children() const { return {}; }

std::string RelParenthesisTerm::ToString() const { return term ? "(" + term->ToString() + ")" : "()"; }

std::vector<std::shared_ptr<RelNode>> RelParenthesisTerm::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (term) children.push_back(term);
  return children;
}

std::shared_ptr<RelNode> RelBoolean::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelBoolean>(self));
}

std::string RelBoolean::ToString() const { return "true"; }

std::vector<std::shared_ptr<RelNode>> RelBoolean::Children() const { return {}; }

std::shared_ptr<RelNode> RelComparison::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelComparison>(self));
}

std::string RelComparison::ToString() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " " + CompOpToString(op) + " " + r;
}

std::vector<std::shared_ptr<RelNode>> RelComparison::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (lhs) children.push_back(lhs);
  if (rhs) children.push_back(rhs);
  return children;
}

std::shared_ptr<RelNode> RelNegation::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelNegation>(self));
}

std::string RelNegation::ToString() const { return formula ? "not " + formula->ToString() : "not (?)"; }

std::vector<std::shared_ptr<RelNode>> RelNegation::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (formula) children.push_back(formula);
  return children;
}

std::shared_ptr<RelNode> RelDisjunction::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelDisjunction>(self));
}

std::string RelDisjunction::ToString() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " or " + r;
}

std::vector<std::shared_ptr<RelNode>> RelDisjunction::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (lhs) children.push_back(lhs);
  if (rhs) children.push_back(rhs);
  return children;
}

std::shared_ptr<RelNode> RelConjunction::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelConjunction>(self));
}

std::string RelConjunction::ToString() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " and " + r;
}

std::vector<std::shared_ptr<RelNode>> RelConjunction::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (lhs) children.push_back(lhs);
  if (rhs) children.push_back(rhs);
  return children;
}

std::shared_ptr<RelNode> RelParen::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelParen>(self));
}

std::string RelParen::ToString() const { return formula ? "(" + formula->ToString() + ")" : "()"; }

std::vector<std::shared_ptr<RelNode>> RelParen::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (formula) children.push_back(formula);
  return children;
}

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

std::vector<std::shared_ptr<RelNode>> RelExistential::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (formula) children.push_back(formula);
  return children;
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

std::vector<std::shared_ptr<RelNode>> RelUniversal::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (formula) children.push_back(formula);
  return children;
}

std::shared_ptr<RelNode> RelFullApplication::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelFullApplication>(self));
}
std::string RelFullApplication::ToString() const {
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

std::vector<std::shared_ptr<RelNode>> RelFullApplication::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (auto* abs_base = dynamic_cast<RelExprApplBase*>(base.get())) {
    if (abs_base->expr) children.push_back(abs_base->expr);
  }
  for (const auto& param : params) {
    if (param) {
      auto expr = param->GetExpr();
      if (expr) children.push_back(expr);
    }
  }
  return children;
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

std::vector<std::shared_ptr<RelNode>> RelProduct::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  for (auto& expr : exprs) {
    if (expr) children.push_back(expr);
  }
  return children;
}

std::shared_ptr<RelNode> RelCondition::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelCondition>(self));
}
std::string RelCondition::ToString() const {
  std::string l = lhs ? lhs->ToString() : "?";
  std::string r = rhs ? rhs->ToString() : "?";
  return l + " where " + r;
}

std::vector<std::shared_ptr<RelNode>> RelCondition::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (lhs) children.push_back(lhs);
  if (rhs) children.push_back(rhs);
  return children;
}

std::shared_ptr<RelNode> RelExprAbstraction::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelExprAbstraction>(self));
}
std::string RelExprAbstraction::ToString() const {
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

std::vector<std::shared_ptr<RelNode>> RelExprAbstraction::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (expr) children.push_back(expr);
  return children;
}

std::shared_ptr<RelNode> RelFormulaAbstraction::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelFormulaAbstraction>(self));
}
std::string RelFormulaAbstraction::ToString() const {
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

std::vector<std::shared_ptr<RelNode>> RelFormulaAbstraction::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (formula) children.push_back(formula);
  return children;
}

std::shared_ptr<RelNode> RelPartialApplication::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelPartialApplication>(self));
}
std::string RelPartialApplication::ToString() const {
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

std::vector<std::shared_ptr<RelNode>> RelPartialApplication::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (auto* abs_base = dynamic_cast<RelExprApplBase*>(base.get())) {
    if (abs_base->expr) children.push_back(abs_base->expr);
  }
  for (const auto& param : params) {
    if (param) {
      auto expr = param->GetExpr();
      if (expr) children.push_back(expr);
    }
  }
  return children;
}

std::shared_ptr<RelNode> RelDef::DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) {
  return visitor.Visit(std::dynamic_pointer_cast<RelDef>(self));
}
std::string RelDef::ToString() const { return "def " + name + " " + (body ? body->ToString() : "{}"); }

std::vector<std::shared_ptr<RelNode>> RelDef::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  if (body) children.push_back(body);
  return children;
}

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

std::vector<std::shared_ptr<RelNode>> RelProgram::Children() const {
  std::vector<std::shared_ptr<RelNode>> children;
  for (auto& def : defs) {
    if (def) children.push_back(def);
  }
  return children;
}

}  // namespace rel2sql
