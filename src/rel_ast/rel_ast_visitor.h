#ifndef REL_AST_REL_AST_VISITOR_H
#define REL_AST_REL_AST_VISITOR_H

#include <memory>

namespace rel2sql {

// Forward declarations

// Abstract nodes
struct RelNode;
struct RelExpr;
struct RelFormula;
struct RelTerm;
struct RelApplParam;
struct RelApplBase;

// Concrete nodes
struct RelProgram;
struct RelDef;
struct RelAbstraction;
struct RelLiteral;
struct RelTermExpr;
struct RelProductExpr;
struct RelConditionExpr;
struct RelAbstractionExpr;
struct RelFormulaExpr;
struct RelBindingsExpr;
struct RelBindingsFormula;
struct RelPartialAppl;
struct RelFormulaBool;
struct RelFullAppl;
struct RelQuantification;
struct RelParen;
struct RelComparison;
struct RelNegation;
struct RelConjunction;
struct RelDisjunction;
struct RelIDTerm;
struct RelNumTerm;
struct RelOpTerm;
struct RelParenthesisTerm;
struct RelUnderscoreParam;
struct RelExprApplParam;
struct RelIDApplBase;
struct RelAbstractionApplBase;
/**
 * Visitor for the Rel AST. Every Visit returns shared_ptr<RelNode> (or derived).
 * - Analysis visitors: return nullptr (side effects only)
 * - Rewriters: return the (possibly replaced) node, or nullptr for identity
 *
 * Default implementation traverses the AST and returns nullptr.
 * Override Visit() for node types you want to customize.
 */
class BaseRelVisitor {
 public:
  virtual ~BaseRelVisitor() = default;

  virtual std::shared_ptr<RelNode> Visit(const std::shared_ptr<RelNode>& node);
  virtual std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelExpr>& node);
  virtual std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFormula>& node);
  virtual std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelTerm>& node);
  virtual std::shared_ptr<RelApplParam> Visit(const std::shared_ptr<RelApplParam>& node);
  virtual std::shared_ptr<RelApplBase> Visit(const std::shared_ptr<RelApplBase>& node);

  // Program level
  virtual std::shared_ptr<RelProgram> Visit(const std::shared_ptr<RelProgram>& node);
  virtual std::shared_ptr<RelDef> Visit(const std::shared_ptr<RelDef>& node);
  virtual std::shared_ptr<RelAbstraction> Visit(const std::shared_ptr<RelAbstraction>& node);

  // Expressions
  virtual std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelLiteral>& node);
  virtual std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelTermExpr>& node);
  virtual std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelProductExpr>& node);
  virtual std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelConditionExpr>& node);
  virtual std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelAbstractionExpr>& node);
  virtual std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelFormulaExpr>& node);
  virtual std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBindingsExpr>& node);
  virtual std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelBindingsFormula>& node);
  virtual std::shared_ptr<RelExpr> Visit(const std::shared_ptr<RelPartialAppl>& node);

  // Formulas
  virtual std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFormulaBool>& node);
  virtual std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelFullAppl>& node);
  virtual std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelQuantification>& node);
  virtual std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelParen>& node);
  virtual std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelComparison>& node);
  virtual std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelNegation>& node);
  virtual std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelConjunction>& node);
  virtual std::shared_ptr<RelFormula> Visit(const std::shared_ptr<RelDisjunction>& node);

  // Terms
  virtual std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelIDTerm>& node);
  virtual std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelNumTerm>& node);
  virtual std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelOpTerm>& node);
  virtual std::shared_ptr<RelTerm> Visit(const std::shared_ptr<RelParenthesisTerm>& node);

  // Appl params
  virtual std::shared_ptr<RelApplParam> Visit(const std::shared_ptr<RelUnderscoreParam>& node);
  virtual std::shared_ptr<RelApplParam> Visit(const std::shared_ptr<RelExprApplParam>& node);

  // Appl bases
  virtual std::shared_ptr<RelApplBase> Visit(const std::shared_ptr<RelIDApplBase>& node);
  virtual std::shared_ptr<RelApplBase> Visit(const std::shared_ptr<RelAbstractionApplBase>& node);
};

}  // namespace rel2sql

#endif  // REL_AST_REL_AST_VISITOR_H
