#ifndef OPTIMIZER_VISITOR_H
#define OPTIMIZER_VISITOR_H

#include "sql_ast/expr_visitor.h"
#include "sql_ast/sql_ast.h"

namespace sql::ast {

class BaseOptimizer : public ExpressionVisitor {
 public:
  BaseOptimizer() = default;
  virtual ~BaseOptimizer() = default;

  virtual void Visit(Expression& expr) override {
    bool is_base_expr = !base_expr_;
    if (is_base_expr) {
      base_expr_ = std::shared_ptr<Expression>(&expr, [](Expression*) {});
    }

    expr.Accept(*this);

    if (is_base_expr) {
      base_expr_.reset();
    }
  }

 protected:
  std::shared_ptr<Expression> base_expr_;
};

class CTEOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(SelectStatement& select_statement) override {
    std::vector<std::shared_ptr<Source>> new_ctes;
    for (auto& cte : select_statement.ctes) {
      if (!TryReplaceRedundantCTE(cte, select_statement)) {
        new_ctes.push_back(cte);
      }
    }
    select_statement.ctes = new_ctes;
  }

 private:
  bool TryReplaceRedundantCTE(const std::shared_ptr<Source>& cte, SelectStatement& select_statement);
};  // class CTEOptimizer

class ConstantOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(FromStatement& from_statement) override {
    std::vector<std::shared_ptr<Source>> new_sources;
    for (auto& source : from_statement.sources) {
      // If the optimization succeeds, the source needs to be removed from the FROM statement
      if (!TryReplaceConstantInWhere(source, from_statement)) {
        new_sources.push_back(source);
      }
    }
    from_statement.sources = new_sources;

    for (auto& source : from_statement.sources) {
      Visit(*source);
    }
  }

 private:
  bool TryReplaceConstantInWhere(const std::shared_ptr<Source>& source, FromStatement& from_statement);
};  // class ConstantOptimizer

class FlattenerOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(SelectStatement& select_statement) override {
    if (select_statement.from.has_value()) {
      for (auto& source : select_statement.from.value()->sources) {
        Visit(*source);
      }
    }

    TryFlattenSubquery(select_statement);
  }

 private:
  bool TryFlattenSubquery(SelectStatement& select_statement);
};  // class FlattenerOptimizer

class SelfJoinOptimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(SelectStatement& select_statement) override {
    if (select_statement.from.has_value()) {
      for (auto& source : select_statement.from.value()->sources) {
        Visit(*source);
      }
    }

    EliminateRedundantSelfJoins(select_statement);
  }

 private:
  bool EliminateRedundantSelfJoins(SelectStatement& select_statement);
  std::unordered_map<std::string, std::vector<std::shared_ptr<Source>>> GroupSourcesByTable(
      const std::vector<std::shared_ptr<Source>>& sources);
  void CollectComparisonConditions(const std::shared_ptr<Condition>& condition,
                                   std::vector<std::shared_ptr<ComparisonCondition>>& comparisons);
};  // class SelfJoinOptimizer

class Optimizer : public BaseOptimizer {
 public:
  using BaseOptimizer::Visit;

  void Visit(SelectStatement& select_statement) override {
    // Cast SelectStatement to Expression
    auto& expression = static_cast<Expression&>(select_statement);

    cte_optimizer_.Visit(expression);

    if (select_statement.from.has_value()) {
      constant_optimizer_.Visit(*select_statement.from.value());
    }

    flattener_optimizer_.Visit(expression);
    // self_join_optimizer_.Visit(expression);
  }

 private:
  CTEOptimizer cte_optimizer_;
  ConstantOptimizer constant_optimizer_;
  FlattenerOptimizer flattener_optimizer_;
  SelfJoinOptimizer self_join_optimizer_;
};  // class Optimizer

}  // namespace sql::ast

#endif  // OPTIMIZER_VISITOR_H
