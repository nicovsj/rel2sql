#ifndef REL_AST_REL_AST_H
#define REL_AST_REL_AST_H

#include <antlr4-runtime.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "rel_ast/bound_set.h"
#include "sql_ast/sql_ast.h"

namespace rel2sql {

// Forward declarations (RelVisitorBase defined in rel_ast_visitor.h)
class BaseRelVisitor;

// =============================================================================
// Enums
// =============================================================================

enum class RelCompOp { EQ, NEQ, LT, GT, LTE, GTE };

enum class RelLogicalOp { AND, OR };

enum class RelQuantOp { EXISTS, FORALL };

enum class RelTermOp { ADD, SUB, MUL, DIV };

// =============================================================================
// Supporting types (used in multiple node types)
// =============================================================================

using RelLiteralValue = std::variant<int, double, std::string, bool>;

// =============================================================================
// Base node with metadata
// =============================================================================

class RelNode {
 public:
  virtual ~RelNode() = default;

  virtual std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) = 0;

  virtual std::string ToString() const = 0;

  // Source location for error reporting (nullptr for synthetic nodes)
  antlr4::ParserRuleContext* ctx = nullptr;

  // Variables bound in the subexpression
  std::set<std::string> variables;
  std::set<std::string> free_variables;

  // Arity of the subexpression
  size_t arity = 0;

  // SQL expression (set during translation)
  std::shared_ptr<sql::ast::Expression> sql_expression;

  // Safety analysis result
  BoundSet safety;

  bool disabled = false;
  std::optional<sql::ast::constant_t> constant;

  bool has_only_literal_values = false;

  bool is_recursive = false;
  std::string recursive_definition_name;

  // Linear term analysis (for terms)
  std::optional<std::pair<double, double>> term_linear_coeffs;
  bool term_linear_invalid = false;

  void VariablesInplaceUnion(const RelNode& other) {
    variables.insert(other.variables.begin(), other.variables.end());
    free_variables.insert(other.free_variables.begin(), other.free_variables.end());
  }

  void VariablesInplaceDifference(const RelNode& other) {
    variables.insert(other.variables.begin(), other.variables.end());
    for (const auto& var : other.free_variables) {
      free_variables.erase(var);
    }
  }

  bool IsInvalidTermExpression() const { return term_linear_invalid; }

  bool IsNullPolynomialTerm() const {
    if (!term_linear_coeffs) return false;
    auto [a, b] = *term_linear_coeffs;
    return a == 0.0 && b == 0.0;
  }

  std::optional<double> GetPolynomialRoot() const {
    if (term_linear_invalid || !term_linear_coeffs) return std::nullopt;
    auto [a, b] = *term_linear_coeffs;
    if (a == 0.0) return std::nullopt;
    return -b / a;
  }

  // Returns direct structural children for traversal.
  virtual std::vector<std::shared_ptr<RelNode>> Children() const = 0;
};

struct RelExpr : RelNode {
  virtual ~RelExpr() = default;
};

struct RelTerm : RelExpr {
  virtual ~RelTerm() = default;
};

struct RelFormula : RelExpr {
  virtual ~RelFormula() = default;
};

struct RelApplParam : RelNode {
  virtual ~RelApplParam() = default;
  virtual bool IsWildcard() const { return false; }
  virtual std::shared_ptr<RelExpr> GetExpr() const { return nullptr; }
};

struct RelApplBase : RelNode {
  virtual ~RelApplBase() = default;
};

struct RelBinding : RelNode {
  virtual ~RelBinding() = default;
};

// =============================================================================
// Literals
// =============================================================================

struct RelLiteral : RelExpr {
  RelLiteralValue value;

  virtual ~RelLiteral() = default;

  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;

  explicit RelLiteral(RelLiteralValue v) : value(std::move(v)) {}

  std::string ToString() const override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

// =============================================================================
// RelUnion - { expr ; expr ; ... }
// =============================================================================

struct RelUnion : RelExpr {
  std::vector<std::shared_ptr<RelExpr>> exprs;

  RelUnion() = default;
  explicit RelUnion(std::vector<std::shared_ptr<RelExpr>> exprs) : exprs(std::move(exprs)) {}

  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;

  std::string ToString() const override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

// =============================================================================
// Terms
// =============================================================================

struct RelIDTerm : RelTerm {
  std::string id;

  explicit RelIDTerm(std::string id) : id(std::move(id)) {}

  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;

  std::string ToString() const override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelNumTerm : RelTerm {
  sql::ast::constant_t value;

  explicit RelNumTerm(sql::ast::constant_t value) : value(std::move(value)) {}

  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;

  std::string ToString() const override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelOpTerm : RelTerm {
  std::shared_ptr<RelTerm> lhs;
  RelTermOp op;
  std::shared_ptr<RelTerm> rhs;

  RelOpTerm(std::shared_ptr<RelTerm> lhs, RelTermOp op, std::shared_ptr<RelTerm> rhs)
      : lhs(std::move(lhs)), op(op), rhs(std::move(rhs)) {}

  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;

  std::string ToString() const override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelParenthesisTerm : RelTerm {
  std::shared_ptr<RelTerm> term;

  explicit RelParenthesisTerm(std::shared_ptr<RelTerm> term) : term(std::move(term)) {}

  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;

  std::string ToString() const override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

// =============================================================================
// Arguments
// =============================================================================

struct RelWildcardParam : RelApplParam {
  std::string ToString() const override { return "_"; }
  bool IsWildcard() const override { return true; }
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};
struct RelExprApplParam : RelApplParam {
  std::shared_ptr<RelExpr> expr;
  explicit RelExprApplParam(std::shared_ptr<RelExpr> e) : expr(std::move(e)) {}
  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::shared_ptr<RelExpr> GetExpr() const override { return expr; }
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

// =============================================================================
// Application bases
// =============================================================================

struct RelIDApplBase : RelApplBase {
  std::string id;
  explicit RelIDApplBase(std::string id) : id(id) {}
  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelExprApplBase : RelApplBase {
  std::shared_ptr<RelUnion> expr;
  explicit RelExprApplBase(std::shared_ptr<RelUnion> expr) : expr(std::move(expr)) {}
  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

// =============================================================================
// Bindings
// =============================================================================

struct RelLiteralBinding : RelBinding {
  RelLiteralValue value;
  explicit RelLiteralBinding(RelLiteralValue v) : value(std::move(v)) {}
  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelVarBinding : RelBinding {
  std::string id;
  std::optional<std::string> domain;
  RelVarBinding(std::string id, std::optional<std::string> domain) : id(std::move(id)), domain(std::move(domain)) {}
  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

// =============================================================================
// Formulas
// =============================================================================

struct RelBoolean : RelFormula {
  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelComparison : RelFormula {
  std::shared_ptr<RelTerm> lhs;
  RelCompOp op;
  std::shared_ptr<RelTerm> rhs;

  RelComparison(std::shared_ptr<RelTerm> lhs, RelCompOp op, std::shared_ptr<RelTerm> rhs)
      : lhs(std::move(lhs)), op(op), rhs(std::move(rhs)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelNegation : RelFormula {
  std::shared_ptr<RelFormula> formula;

  explicit RelNegation(std::shared_ptr<RelFormula> formula) : formula(std::move(formula)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelConjunction : RelFormula {
  std::shared_ptr<RelFormula> lhs;
  std::shared_ptr<RelFormula> rhs;

  RelConjunction(std::shared_ptr<RelFormula> lhs, std::shared_ptr<RelFormula> rhs)
      : lhs(std::move(lhs)), rhs(std::move(rhs)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelDisjunction : RelFormula {
  std::shared_ptr<RelFormula> lhs;
  std::shared_ptr<RelFormula> rhs;

  RelDisjunction(std::shared_ptr<RelFormula> lhs, std::shared_ptr<RelFormula> rhs)
      : lhs(std::move(lhs)), rhs(std::move(rhs)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelParen : RelFormula {
  std::shared_ptr<RelFormula> formula;

  explicit RelParen(std::shared_ptr<RelFormula> formula) : formula(std::move(formula)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelExistential : RelFormula {
  std::vector<std::shared_ptr<RelBinding>> bindings;
  std::shared_ptr<RelFormula> formula;

  RelExistential(std::vector<std::shared_ptr<RelBinding>> bindings, std::shared_ptr<RelFormula> formula)
      : bindings(std::move(bindings)), formula(std::move(formula)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelUniversal : RelFormula {
  std::vector<std::shared_ptr<RelBinding>> bindings;
  std::shared_ptr<RelFormula> formula;

  RelUniversal(std::vector<std::shared_ptr<RelBinding>> bindings, std::shared_ptr<RelFormula> formula)
      : bindings(std::move(bindings)), formula(std::move(formula)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelFullApplication : RelFormula {
  std::shared_ptr<RelApplBase> base;
  std::vector<std::shared_ptr<RelApplParam>> params;

  RelFullApplication(std::shared_ptr<RelApplBase> base, std::vector<std::shared_ptr<RelApplParam>> params)
      : base(std::move(base)), params(std::move(params)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

// =============================================================================
// Expressions
// =============================================================================

struct RelProduct : RelExpr {
  std::vector<std::shared_ptr<RelExpr>> exprs;

  RelProduct() = default;
  explicit RelProduct(std::vector<std::shared_ptr<RelExpr>> exprs) : exprs(std::move(exprs)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelCondition : RelExpr {
  std::shared_ptr<RelExpr> lhs;
  std::shared_ptr<RelFormula> rhs;

  RelCondition(std::shared_ptr<RelExpr> lhs, std::shared_ptr<RelFormula> rhs)
      : lhs(std::move(lhs)), rhs(std::move(rhs)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelExprAbstraction : RelExpr {
  std::vector<std::shared_ptr<RelBinding>> bindings;
  std::shared_ptr<RelExpr> expr;

  RelExprAbstraction(std::vector<std::shared_ptr<RelBinding>> bindings, std::shared_ptr<RelExpr> expr)
      : bindings(std::move(bindings)), expr(std::move(expr)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelFormulaAbstraction : RelExpr {
  std::vector<std::shared_ptr<RelBinding>> bindings;
  std::shared_ptr<RelFormula> formula;

  RelFormulaAbstraction(std::vector<std::shared_ptr<RelBinding>> bindings, std::shared_ptr<RelFormula> formula)
      : bindings(std::move(bindings)), formula(std::move(formula)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelPartialApplication : RelExpr {
  std::shared_ptr<RelApplBase> base;
  std::vector<std::shared_ptr<RelApplParam>> params;

  RelPartialApplication(std::shared_ptr<RelApplBase> base, std::vector<std::shared_ptr<RelApplParam>> params)
      : base(std::move(base)), params(std::move(params)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

// =============================================================================
// Program level
// =============================================================================

struct RelDef : RelNode {
  std::string name;
  std::shared_ptr<RelUnion> body;

  std::vector<std::shared_ptr<RelUnion>> multiple_defs;

  RelDef(std::string name, std::shared_ptr<RelUnion> body) : name(std::move(name)), body(std::move(body)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

struct RelProgram : RelNode {
  std::vector<std::shared_ptr<RelDef>> defs;

  RelProgram() = default;
  explicit RelProgram(std::vector<std::shared_ptr<RelDef>>& defs) : defs(std::move(defs)) {}

  std::string ToString() const override;
  std::shared_ptr<RelNode> DispatchVisit(BaseRelVisitor& visitor, std::shared_ptr<RelNode> self) override;
  std::vector<std::shared_ptr<RelNode>> Children() const override;
};

}  // namespace rel2sql

#endif  // REL_AST_REL_AST_H
