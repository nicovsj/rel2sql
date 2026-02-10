#ifndef SQL_SQL_VISITOR_REL_H
#define SQL_SQL_VISITOR_REL_H

#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_container.h"
#include "rel_ast/rel_ast_visitor.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {

/**
 * SQLVisitor that operates on typed RelAST nodes.
 * Produces sql::ast::Expression from RelProgram.
 *
 * This is the Rel-pipeline replacement for SQLVisitor.
 * Currently implements a subset of translations; more cases added incrementally.
 */
class SQLVisitorRel : public RelASTVisitor {
 public:
  explicit SQLVisitorRel(RelASTContainer* container) : container_(container) {}

  std::shared_ptr<sql::ast::Expression> Translate(RelProgram& program);
  std::shared_ptr<sql::ast::Expression> TranslateFormula(RelFormula& formula);
  std::shared_ptr<sql::ast::Expression> TranslateExpr(RelExpr& expr);

  void Visit(RelProgram& node) override;
  void Visit(RelDef& node) override;
  void Visit(RelAbstraction& node) override;
  void Visit(RelLitExpr& node) override;
  void Visit(RelProductExpr& node) override;
  void Visit(RelTermExpr& node) override;
  void Visit(RelConditionExpr& node) override;
  void Visit(RelAbstractionExpr& node) override;
  void Visit(RelFormulaExpr& node) override;
  void Visit(RelBindingsExpr& node) override;
  void Visit(RelBindingsFormula& node) override;
  void Visit(RelPartialAppl& node) override;
  void Visit(RelFullAppl& node) override;
  void Visit(RelBinOp& node) override;
  void Visit(RelUnOp& node) override;
  void Visit(RelQuantification& node) override;
  void Visit(RelParen& node) override;
  void Visit(RelComparison& node) override;
  void Visit(RelLiteral& node) override;
  void Visit(RelFormulaBool& node) override;
  void Visit(RelIDTerm& node) override;
  void Visit(RelNumTerm& node) override;
  void Visit(RelOpTerm& node) override;
  void Visit(RelParenthesisTerm& node) override;

 private:
  std::shared_ptr<sql::ast::Sourceable> TryGetTopLevelIDSelect(RelAbstraction* body);
  // Build a VALUES-based Select from a relation abstraction that has only literal/product exprs.
  std::shared_ptr<sql::ast::Expression> BuildLiteralRelationAbstractionRel(RelAbstraction& node);
  std::shared_ptr<sql::ast::Expression> GetExpressionFromID(RelNode& node, const std::string& id, bool is_top_level);
  // Resolve the base of an application (ID or relation abstraction) to a Sourceable. Shared by full and partial
  // application.
  std::shared_ptr<sql::ast::Sourceable> GetBaseSourceableFromApplBase(RelNode& node,
                                                                      const std::shared_ptr<RelApplBase>& base);
  // Split application params into term (variable/constant), relation (ID = relation), and non-term (expr) slots. Shared
  // by full and partial application.
  struct FullApplParamSlots {
    std::vector<std::pair<RelNode*, size_t>> term_param_slots;
    std::vector<std::pair<size_t, std::shared_ptr<sql::ast::Source>>> relation_param_sources;
    std::vector<std::tuple<size_t, std::shared_ptr<sql::ast::Source>, RelNode*>> non_term_param_slots;
  };
  FullApplParamSlots CollectApplParams(RelNode& node, const std::vector<std::shared_ptr<RelApplParam>>& params);
  FullApplParamSlots CollectFullApplParams(RelFullAppl& node);
  // Build FROM sources, WHERE condition, and SELECT list for an application (full or partial). Mirrors
  // ApplicationVariableConditions + SpecialAppliedVarList.
  struct FullApplSqlParts {
    std::vector<std::shared_ptr<sql::ast::Source>> from_sources;
    std::shared_ptr<sql::ast::Condition> where;
    std::vector<std::shared_ptr<sql::ast::Selectable>> select_cols;
  };
  FullApplSqlParts BuildFullApplSql(const FullApplParamSlots& slots, const std::shared_ptr<sql::ast::Source>& ra_source,
                                    const std::shared_ptr<sql::ast::Sourceable>& base_sourceable,
                                    const std::function<std::string(size_t)>& column_name_for_index);
  // Build SELECT with GROUP BY and aggregate (for partial application of aggregate functions, e.g. sum[A]).
  std::shared_ptr<sql::ast::Select> VisitAggregateRel(RelExpr& expr, sql::ast::AggregateFunction function);
  std::string GenerateTableAlias(const std::string& prefix = "T");
  // Return the column name for the idx-th column (1-based) of a sourceable (Table, Select, Union, etc.).
  std::string GetColumnNameForSourceable(const std::shared_ptr<sql::ast::Sourceable>& src, size_t idx) const;
  // Return the number of columns (arity) of a sourceable.
  size_t GetArityForSourceable(const std::shared_ptr<sql::ast::Sourceable>& src) const;
  std::shared_ptr<sql::ast::Source> CreateTableSource(const std::string& table_name);
  void ApplyDistinctToDefinitionSelects(const std::shared_ptr<sql::ast::Sourceable>& sourceable);
  std::vector<std::shared_ptr<sql::ast::Selectable>> VarListShorthandRel(
      const std::vector<RelNode*>& nodes, const std::shared_ptr<sql::ast::Source>& source);
  std::vector<std::shared_ptr<sql::ast::Selectable>> VarListShorthandRel(
      const std::vector<std::pair<RelNode*, std::shared_ptr<sql::ast::Source>>>& node_source_pairs);
  std::shared_ptr<sql::ast::Condition> EqualityShorthandRel(const std::vector<RelNode*>& nodes);
  // Condition expr special case: RHS is (possibly parenthesized) conjunction of comparisons only.
  bool CollectComparatorOnlyConjunctsRel(const std::shared_ptr<RelFormula>& formula,
                                         std::vector<std::shared_ptr<RelNode>>& out);
  void BuildConditionExprComparatorOnlyRHSRel(RelConditionExpr& node,
                                               const std::vector<std::shared_ptr<RelNode>>& comparator_conjuncts);
  // For a full application, build chained equalities between columns corresponding
  // to repeated term parameters for the same variable (p1 = p2, p2 = p3, ...).
  std::vector<std::shared_ptr<sql::ast::Condition>> AddChainedEqualitiesForTermParams(
      const std::vector<std::pair<RelNode*, size_t>>& term_param_slots,
      const std::function<std::string(size_t)>& column_name_for_index,
      const std::shared_ptr<sql::ast::Source>& ra_source);
  // Generalized disjunction (OR) over two subformulas: translate both and union their results.
  std::shared_ptr<sql::ast::Expression> VisitGeneralizedDisjunctionRel(const std::shared_ptr<RelFormula>& lhs,
                                                                       const std::shared_ptr<RelFormula>& rhs);
  // Conjunction with separated comparator/non-comparator conjuncts, as produced by balancing.
  std::shared_ptr<sql::ast::Expression> VisitConjunctionWithComparatorsRel(
      const std::vector<std::shared_ptr<RelNode>>& other, const std::vector<std::shared_ptr<RelNode>>& comparators);
  // Simple binary conjunction without term/comparator splitting.
  std::shared_ptr<sql::ast::Expression> VisitSimpleBinaryRel(const std::shared_ptr<RelFormula>& lhs,
                                                             const std::shared_ptr<RelFormula>& rhs);
  // Conjunction with negated conjuncts: non_negated AND NOT negated_1 AND NOT negated_2 ... (NOT IN subqueries).
  std::shared_ptr<sql::ast::Expression> VisitConjunctionWithNegationsRel(
      const std::vector<std::shared_ptr<RelNode>>& non_negated, const std::vector<std::shared_ptr<RelNode>>& negated);
  // Existential quantification: exists bindings. formula
  std::shared_ptr<sql::ast::Expression> VisitExistentialRel(const std::vector<std::shared_ptr<RelBinding>>& bindings,
                                                            const std::shared_ptr<RelFormula>& formula,
                                                            const std::set<std::string>& free_vars);
  // Universal quantification: forall bindings. formula
  std::shared_ptr<sql::ast::Expression> VisitUniversalRel(const std::vector<std::shared_ptr<RelBinding>>& bindings,
                                                          const std::shared_ptr<RelFormula>& formula,
                                                          const std::set<std::string>& free_vars);
  std::shared_ptr<sql::ast::Expression> VisitGeneralizedConjunctionRel(
      const std::vector<std::shared_ptr<RelNode>>& subformulas);
  void SpecialAddSourceToFreeVariablesInTerm(
      const std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>>& free_var_sources,
      std::shared_ptr<sql::ast::Term>& term);

  RelASTContainer* container_;
  std::shared_ptr<sql::ast::Expression> result_;
  std::unordered_map<std::string, int> table_alias_prefix_counter_;
};

}  // namespace rel2sql

#endif  // SQL_SQL_VISITOR_REL_H
