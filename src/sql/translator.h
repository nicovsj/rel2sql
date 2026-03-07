#ifndef SQL_SQL_VISITOR_REL_H
#define SQL_SQL_VISITOR_REL_H

#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_visitor.h"
#include "rel_ast/rel_context.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {

/**
 * Translator that translates RelAST nodes to sql::ast::Expression.
 * Produces sql::ast::Expression from RelNode.
 */
class Translator : public BaseRelVisitor {
 public:
  using BaseRelVisitor::Visit;  // Avoid name shadowing

  explicit Translator(const RelContext& context) : context_(context) {}

  std::shared_ptr<sql::ast::Expression> Translate();

  // Rules
  std::shared_ptr<RelProgram> Visit(const std::shared_ptr<RelProgram>& node) override;
  std::shared_ptr<RelDef> Visit(const std::shared_ptr<RelDef>& node) override;

  // Abstraction
  std::shared_ptr<RelAbstraction> Visit(const std::shared_ptr<RelAbstraction>& node) override;

  // Expressions
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelLiteral>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelProduct>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelConditionExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelAbstractionExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBindingsExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBindingsFormula>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelFormulaExpr>& node) override;
  std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelPartialAppl>& node) override;

  // Formulas
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullAppl>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelConjunction>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelDisjunction>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelNegation>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelExistential>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelUniversal>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelParen>& node) override;
  std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelComparison>& node) override;

  // Terms
  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelIDTerm>& node) override;
  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelNumTerm>& node) override;
  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelOpTerm>& node) override;
  std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelParenthesisTerm>& node) override;

 private:
  std::shared_ptr<sql::ast::Sourceable> TryGetTopLevelIDSelect(RelAbstraction* body);

  // Build a VALUES-based Select from a relation abstraction that has only literal/product exprs.
  std::shared_ptr<sql::ast::Expression> BuildLiteralRelationAbstractionRel(const std::shared_ptr<RelAbstraction>& node);

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

  // Build SQL term for a variable from a param slot column using term_linear_coeffs (column holds a*x+b, result is x).
  std::shared_ptr<sql::ast::Term> MakeTermForVariableFromParamSlotRel(
      RelNode* term_node, const std::string& column_name, const std::shared_ptr<sql::ast::Source>& ra_source) const;

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
  std::shared_ptr<sql::ast::Select> VisitAggregateRel(const std::shared_ptr<RelExpr>& expr,
                                                      sql::ast::AggregateFunction function);

  std::string GenerateTableAlias(const std::string& prefix = "T");

  // Return the column name for the idx-th column (1-based) of a sourceable (Table, Select, Union, etc.).
  std::string GetColumnNameForSourceable(const std::shared_ptr<sql::ast::Sourceable>& src, size_t idx) const;

  // Return the number of columns (arity) of a sourceable.
  size_t GetArityForSourceable(const std::shared_ptr<sql::ast::Sourceable>& src) const;

  // Cast expr to Sourceable; throw TranslationException if the cast fails.
  std::shared_ptr<sql::ast::Sourceable> ExpectSourceable(const std::shared_ptr<sql::ast::Expression>& expr) const;

  std::shared_ptr<sql::ast::Source> CreateTableSource(const std::string& table_name);

  void ApplyDistinctToDefinitionSelects(const std::shared_ptr<sql::ast::Sourceable>& sourceable);

  std::vector<std::shared_ptr<sql::ast::Selectable>> VarListShorthandRel(
      const std::vector<RelNode*>& nodes, const std::shared_ptr<sql::ast::Source>& source);

  std::vector<std::shared_ptr<sql::ast::Selectable>> VarListShorthandRel(
      const std::vector<std::pair<RelNode*, std::shared_ptr<sql::ast::Source>>>& node_source_pairs);

  std::shared_ptr<sql::ast::Condition> EqualityShorthandRel(const std::vector<RelNode*>& nodes);

  // For a full application, build chained equalities between columns corresponding
  // to repeated term parameters for the same variable (p1 = p2, p2 = p3, ...).
  std::vector<std::shared_ptr<sql::ast::Condition>> AddChainedEqualitiesForTermParams(
      const std::vector<std::pair<RelNode*, size_t>>& term_param_slots,
      const std::function<std::string(size_t)>& column_name_for_index,
      const std::shared_ptr<sql::ast::Source>& ra_source);

  std::shared_ptr<sql::ast::Expression> VisitGeneralizedConjunctionRel(
      const std::vector<std::shared_ptr<RelNode>>& subformulas);

  void SpecialAddSourceToFreeVariablesInTerm(
      const std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>>& free_var_sources,
      std::shared_ptr<sql::ast::Term>& term);

  // For inferrable term equalities: build SQL term from a linear RelTerm given variable->source map.
  std::shared_ptr<sql::ast::Term> BuildSqlTermFromLinearRelTerm(
      const std::shared_ptr<RelTerm>& rel_term,
      const std::unordered_map<std::string, std::shared_ptr<sql::ast::Source>>& free_var_sources) const;

  // Create a recursive CTE from a formula (bindings formula with is_recursive). Returns (CTE source, any CTEs from
  // the formula).
  std::pair<std::shared_ptr<sql::ast::Source>, std::vector<std::shared_ptr<sql::ast::Source>>>
  CreateRecursiveCTEFromFormula(const std::shared_ptr<sql::ast::Sourceable>& formula_sql,
                                const std::string& recursive_definition_name, int arity);

  // Build the formula source for a bindings formula: recursive CTE wrapped in a var-renaming subquery, or plain
  // Source(formula_sql). When recursive, out_ctes and out_ctes_are_recursive are set so the caller can attach the
  // CTE to the outer SELECT.
  std::shared_ptr<sql::ast::Source> BuildBindingsFormulaSource(
      const std::shared_ptr<sql::ast::Sourceable>& formula_sql, bool is_recursive,
      const std::string& recursive_definition_name, const std::vector<std::shared_ptr<RelBinding>>& bindings,
      std::vector<std::shared_ptr<sql::ast::Source>>* out_ctes = nullptr, bool* out_ctes_are_recursive = nullptr);

  const RelContext& context_;
  std::unordered_map<std::string, int> table_alias_prefix_counter_;
};

}  // namespace rel2sql

#endif  // SQL_SQL_VISITOR_REL_H
