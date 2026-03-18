#include "expression_simplifier_optimizer.h"

#include <cmath>
#include <optional>
#include <vector>

#include "sql_ast/sql_ast.h"
#include "support/utils.h"

namespace rel2sql {
namespace sql::ast {

namespace {

// Returns a new Constant with value a + b, or nullptr if not both numeric.
std::shared_ptr<Constant> AddConstants(const Constant& a, const Constant& b) {
  return std::visit(
      utl::overloaded{
          [](int ai, int bi) -> std::shared_ptr<Constant> { return std::make_shared<Constant>(ai + bi); },
          [](double ad, double bd) -> std::shared_ptr<Constant> { return std::make_shared<Constant>(ad + bd); },
          [](const auto&, const auto&) -> std::shared_ptr<Constant> { return nullptr; }},
      a.value, b.value);
}

// Returns a new Constant with value a - b, or nullptr if not both numeric.
std::shared_ptr<Constant> SubConstants(const Constant& a, const Constant& b) {
  return std::visit(
      utl::overloaded{
          [](int ai, int bi) -> std::shared_ptr<Constant> { return std::make_shared<Constant>(ai - bi); },
          [](double ad, double bd) -> std::shared_ptr<Constant> { return std::make_shared<Constant>(ad - bd); },
          [](const auto&, const auto&) -> std::shared_ptr<Constant> { return nullptr; }},
      a.value, b.value);
}

// Returns a new Constant with value a * b, or nullptr if not both numeric.
std::shared_ptr<Constant> MulConstants(const Constant& a, const Constant& b) {
  return std::visit(
      utl::overloaded{
          [](int ai, int bi) -> std::shared_ptr<Constant> { return std::make_shared<Constant>(ai * bi); },
          [](double ad, double bd) -> std::shared_ptr<Constant> { return std::make_shared<Constant>(ad * bd); },
          [](const auto&, const auto&) -> std::shared_ptr<Constant> { return nullptr; }},
      a.value, b.value);
}

bool IsZero(const Constant& c) {
  return std::visit(utl::overloaded{[](int i) { return i == 0; }, [](double d) { return d == 0.0; },
                                    [](const auto&) { return false; }},
                    c.value);
}

bool IsNegative(const Constant& c) {
  return std::visit(
      utl::overloaded{[](int i) { return i < 0; }, [](double d) { return d < 0.0; }, [](const auto&) { return false; }},
      c.value);
}

std::shared_ptr<Constant> NegateConstant(const Constant& c) {
  return std::visit(
      utl::overloaded{[](int i) -> std::shared_ptr<Constant> { return std::make_shared<Constant>(-i); },
                      [](double d) -> std::shared_ptr<Constant> { return std::make_shared<Constant>(-d); },
                      [](const auto&) -> std::shared_ptr<Constant> { return nullptr; }},
      c.value);
}

bool ConstantsEqual(const Constant& a, const Constant& b) {
  return std::visit(
      utl::overloaded{[](int ai, int bi) { return ai == bi; }, [](double ad, double bd) { return ad == bd; },
                      [](const std::string& as, const std::string& bs) { return as == bs; },
                      [](bool ab, bool bb) { return ab == bb; }, [](const auto&, const auto&) { return false; }},
      a.value, b.value);
}

// Returns true if both sides are constants and the comparison is always true.
bool IsTautology(const ComparisonCondition& c) {
  auto lhs_const = std::dynamic_pointer_cast<const Constant>(c.lhs);
  auto rhs_const = std::dynamic_pointer_cast<const Constant>(c.rhs);
  if (!lhs_const || !rhs_const) return false;

  return std::visit(utl::overloaded{[&](int ai, int bi) {
                                      switch (c.op) {
                                        case CompOp::EQ:
                                          return ai == bi;
                                        case CompOp::NEQ:
                                          return ai != bi;
                                        case CompOp::LT:
                                          return ai < bi;
                                        case CompOp::GT:
                                          return ai > bi;
                                        case CompOp::LTE:
                                          return ai <= bi;
                                        case CompOp::GTE:
                                          return ai >= bi;
                                      }
                                    },
                                    [&](double ad, double bd) {
                                      switch (c.op) {
                                        case CompOp::EQ:
                                          return ad == bd;
                                        case CompOp::NEQ:
                                          return ad != bd;
                                        case CompOp::LT:
                                          return ad < bd;
                                        case CompOp::GT:
                                          return ad > bd;
                                        case CompOp::LTE:
                                          return ad <= bd;
                                        case CompOp::GTE:
                                          return ad >= bd;
                                      }
                                    },
                                    [&](const std::string& as, const std::string& bs) {
                                      switch (c.op) {
                                        case CompOp::EQ:
                                          return as == bs;
                                        case CompOp::NEQ:
                                          return as != bs;
                                        case CompOp::LT:
                                          return as < bs;
                                        case CompOp::GT:
                                          return as > bs;
                                        case CompOp::LTE:
                                          return as <= bs;
                                        case CompOp::GTE:
                                          return as >= bs;
                                      }
                                    },
                                    [&](bool ab, bool bb) {
                                      switch (c.op) {
                                        case CompOp::EQ:
                                          return ab == bb;
                                        case CompOp::NEQ:
                                          return ab != bb;
                                        case CompOp::LT:
                                          return ab < bb;
                                        case CompOp::GT:
                                          return ab > bb;
                                        case CompOp::LTE:
                                          return ab <= bb;
                                        case CompOp::GTE:
                                          return ab >= bb;
                                      }
                                    },
                                    [](const auto&, const auto&) { return false; }},
                    lhs_const->value, rhs_const->value);
}

// Simplifies a term in place. Returns true if the term was modified.
bool SimplifyTerm(std::shared_ptr<Term>& term) {
  if (!term) return false;

  auto op = std::dynamic_pointer_cast<Operation>(term);
  if (!op) {
    // Recurse into ParenthesisTerm so we can simplify inner expressions like 3 * (2*x - 1 + 5*x)
    if (auto paren = std::dynamic_pointer_cast<ParenthesisTerm>(term)) {
      return SimplifyTerm(paren->term);
    }
    return false;
  }

  // Recursively simplify children first
  bool changed = false;
  auto simplify_child = [&changed](std::shared_ptr<Term>& child) {
    if (auto paren = std::dynamic_pointer_cast<ParenthesisTerm>(child)) {
      if (SimplifyTerm(paren->term)) changed = true;
    } else if (SimplifyTerm(child)) {
      changed = true;
    }
  };
  simplify_child(op->lhs);
  simplify_child(op->rhs);

  auto lhs_const = std::dynamic_pointer_cast<Constant>(op->lhs);
  auto rhs_const = std::dynamic_pointer_cast<Constant>(op->rhs);

  // Term / 1 -> Term, Term * 1 -> Term, 1 * Term -> Term
  if (op->op == "/" && rhs_const) {
    int vi = 0;
    double vd = 0;
    if (std::holds_alternative<int>(rhs_const->value)) {
      vi = std::get<int>(rhs_const->value);
      if (vi == 1) {
        term = op->lhs;
        return true;
      }
    } else if (std::holds_alternative<double>(rhs_const->value)) {
      vd = std::get<double>(rhs_const->value);
      if (vd == 1.0) {
        term = op->lhs;
        return true;
      }
    }
  }
  if (op->op == "*") {
    if (lhs_const && rhs_const) {
      auto result = MulConstants(*lhs_const, *rhs_const);
      if (result) {
        term = result;
        return true;
      }
    }
    if (rhs_const &&
        ((std::holds_alternative<int>(rhs_const->value) && std::get<int>(rhs_const->value) == 1) ||
         (std::holds_alternative<double>(rhs_const->value) && std::get<double>(rhs_const->value) == 1.0))) {
      term = op->lhs;
      return true;
    }
    if (lhs_const &&
        ((std::holds_alternative<int>(lhs_const->value) && std::get<int>(lhs_const->value) == 1) ||
         (std::holds_alternative<double>(lhs_const->value) && std::get<double>(lhs_const->value) == 1.0))) {
      term = op->rhs;
      return true;
    }
  }

  // Only handle + and - for the rest
  if (op->op != "+" && op->op != "-") return changed;

  if (lhs_const && rhs_const) {
    std::shared_ptr<Constant> result;
    if (op->op == "+") {
      result = AddConstants(*lhs_const, *rhs_const);
    } else {
      result = SubConstants(*lhs_const, *rhs_const);
    }
    if (result) {
      term = result;
      return true;
    }
  }

  // Constant(0) + Term -> Term
  if (op->op == "+" && lhs_const && IsZero(*lhs_const)) {
    term = op->rhs;
    return true;
  }
  // Term + Constant(0) -> Term
  if (op->op == "+" && rhs_const && IsZero(*rhs_const)) {
    term = op->lhs;
    return true;
  }
  // Constant(c) + Column -> Column + Constant(c) when c is non-negative (normalize for commutativity)
  if (op->op == "+" && lhs_const && !IsNegative(*lhs_const) && std::dynamic_pointer_cast<Column>(op->rhs)) {
    term = std::make_shared<Operation>(op->rhs, op->lhs, "+");
    return true;
  }
  // Constant(c) + Column -> Column - Constant(-c) when c is negative (normalize to col - const)
  if (op->op == "+" && lhs_const && IsNegative(*lhs_const) && std::dynamic_pointer_cast<Column>(op->rhs)) {
    auto neg = NegateConstant(*lhs_const);
    if (neg) {
      term = std::make_shared<Operation>(op->rhs, neg, "-");
      return true;
    }
  }
  if (op->op == "+" && rhs_const && IsNegative(*rhs_const) && std::dynamic_pointer_cast<Column>(op->lhs)) {
    auto neg = NegateConstant(*rhs_const);
    if (neg) {
      term = std::make_shared<Operation>(op->lhs, neg, "-");
      return true;
    }
  }
  // Term - Constant(0) -> Term
  if (op->op == "-" && rhs_const && IsZero(*rhs_const)) {
    term = op->lhs;
    return true;
  }
  // Term - Constant(-c) -> Term + Constant(c) (e.g. col - -1 -> col + 1)
  if (op->op == "-" && rhs_const && IsNegative(*rhs_const)) {
    auto neg = NegateConstant(*rhs_const);
    if (neg) {
      term = std::make_shared<Operation>(op->lhs, neg, "+");
      return true;
    }
  }

  // (Term - Constant(a)) + Constant(b) when a == b -> Term
  if (op->op == "+" && rhs_const) {
    auto inner_op = std::dynamic_pointer_cast<Operation>(op->lhs);
    if (inner_op && inner_op->op == "-") {
      auto inner_rhs_const = std::dynamic_pointer_cast<Constant>(inner_op->rhs);
      if (inner_rhs_const && inner_rhs_const->value == rhs_const->value) {
        term = inner_op->lhs;
        return true;
      }
    }
  }

  // (Term + Constant(a)) - Constant(a) -> Term (e.g. col + 1 - 1 -> col, enables self-join detection)
  if (op->op == "-" && rhs_const) {
    auto inner_op = std::dynamic_pointer_cast<Operation>(op->lhs);
    if (inner_op && inner_op->op == "+") {
      auto inner_rhs_const = std::dynamic_pointer_cast<Constant>(inner_op->rhs);
      if (inner_rhs_const && inner_rhs_const->value == rhs_const->value) {
        term = inner_op->lhs;
        return true;
      }
      auto inner_lhs_const = std::dynamic_pointer_cast<Constant>(inner_op->lhs);
      if (inner_lhs_const && inner_lhs_const->value == rhs_const->value) {
        term = inner_op->rhs;
        return true;
      }
    }
  }

  // (Constant(a) + non_const) + Constant(b) when a+b=0 -> non_const
  if (op->op == "+" && rhs_const) {
    auto inner_op = std::dynamic_pointer_cast<Operation>(op->lhs);
    if (inner_op && inner_op->op == "+") {
      auto inner_const = std::dynamic_pointer_cast<Constant>(inner_op->lhs);
      if (inner_const) {
        auto sum = AddConstants(*inner_const, *rhs_const);
        if (sum && IsZero(*sum)) {
          term = inner_op->rhs;
          return true;
        }
      }
      inner_const = std::dynamic_pointer_cast<Constant>(inner_op->rhs);
      if (inner_const) {
        auto sum = AddConstants(*inner_const, *rhs_const);
        if (sum && IsZero(*sum)) {
          term = inner_op->lhs;
          return true;
        }
      }
    }
  }
  // Constant(a) + (Constant(b) + non_const) when a+b=0 -> non_const
  if (op->op == "+" && lhs_const) {
    auto inner_op = std::dynamic_pointer_cast<Operation>(op->rhs);
    if (inner_op && inner_op->op == "+") {
      auto inner_const = std::dynamic_pointer_cast<Constant>(inner_op->lhs);
      if (inner_const) {
        auto sum = AddConstants(*lhs_const, *inner_const);
        if (sum && IsZero(*sum)) {
          term = inner_op->rhs;
          return true;
        }
      }
      inner_const = std::dynamic_pointer_cast<Constant>(inner_op->rhs);
      if (inner_const) {
        auto sum = AddConstants(*lhs_const, *inner_const);
        if (sum && IsZero(*sum)) {
          term = inner_op->lhs;
          return true;
        }
      }
    }
  }

  return changed;
}

}  // namespace

void ExpressionSimplifierOptimizer::Visit(Select& select) {
  // Visit CTEs first so we simplify conditions inside them (e.g. E0._x0 = 3*(2*E1.x-1+5*E1.x)+E1.x)
  for (auto& cte : select.ctes) {
    Visit(*cte);
  }
  ExpressionVisitor::Visit(select);
}

void ExpressionSimplifierOptimizer::Visit(Operation& operation) {
  while (SimplifyTerm(operation.lhs)) {
  }
  while (SimplifyTerm(operation.rhs)) {
  }
}

void ExpressionSimplifierOptimizer::Visit(ComparisonCondition& comparison_condition) {
  ExpressionVisitor::Visit(comparison_condition);
  while (SimplifyTerm(comparison_condition.lhs)) {
  }
  while (SimplifyTerm(comparison_condition.rhs)) {
  }
}

void ExpressionSimplifierOptimizer::Visit(TermSelectable& term_selectable) {
  ExpressionVisitor::Visit(term_selectable);
  while (SimplifyTerm(term_selectable.term)) {
  }
}

void ExpressionSimplifierOptimizer::Visit(ParenthesisTerm& parenthesis_term) {
  ExpressionVisitor::Visit(parenthesis_term);
  while (SimplifyTerm(parenthesis_term.term)) {
  }
}

void ExpressionSimplifierOptimizer::Visit(Function& function) {
  ExpressionVisitor::Visit(function);
  while (SimplifyTerm(function.arg)) {
  }
}

void ExpressionSimplifierOptimizer::Visit(CaseWhen& case_when) {
  ExpressionVisitor::Visit(case_when);
  for (auto& [condition, term] : case_when.cases) {
    SimplifyTerm(term);
  }
}

void ExpressionSimplifierOptimizer::Visit(LogicalCondition& logical_condition) {
  ExpressionVisitor::Visit(logical_condition);
  // For AND: remove tautologies (cond AND true = cond)
  if (logical_condition.op == LogicalOp::AND) {
    logical_condition.conditions.erase(
        std::remove_if(logical_condition.conditions.begin(), logical_condition.conditions.end(),
                       [](const std::shared_ptr<Condition>& cond) {
                         if (cond->IsEmpty()) return true;
                         auto* comp = dynamic_cast<ComparisonCondition*>(cond.get());
                         return comp && IsTautology(*comp);
                       }),
        logical_condition.conditions.end());
  }
}

void ExpressionSimplifierOptimizer::Visit(From& from) {
  ExpressionVisitor::Visit(from);
  if (from.where) {
    if (from.where.value()->IsEmpty()) {
      from.where = std::nullopt;
    } else if (auto* comp = dynamic_cast<ComparisonCondition*>(from.where.value().get()); comp && IsTautology(*comp)) {
      from.where = std::nullopt;
    }
  }
}

}  // namespace sql::ast
}  // namespace rel2sql
