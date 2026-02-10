#ifndef REL_AST_REL_AST_VISITOR_H
#define REL_AST_REL_AST_VISITOR_H

namespace rel2sql {

// Forward declarations
struct RelProgram;
struct RelDef;
struct RelAbstraction;
struct RelLiteral;
struct RelLitExpr;
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
struct RelUnOp;
struct RelBinOp;
struct RelIDTerm;
struct RelNumTerm;
struct RelOpTerm;
struct RelParenthesisTerm;

class RelASTVisitor {
 public:
  virtual ~RelASTVisitor() = default;

  // Program level - default implementations traverse children
  virtual void Visit(RelProgram& node);
  virtual void Visit(RelDef& node);
  virtual void Visit(RelAbstraction& node);

  // Literals
  virtual void Visit(RelLiteral& node);

  // Expressions
  virtual void Visit(RelLitExpr& node);
  virtual void Visit(RelTermExpr& node);
  virtual void Visit(RelProductExpr& node);
  virtual void Visit(RelConditionExpr& node);
  virtual void Visit(RelAbstractionExpr& node);
  virtual void Visit(RelFormulaExpr& node);
  virtual void Visit(RelBindingsExpr& node);
  virtual void Visit(RelBindingsFormula& node);
  virtual void Visit(RelPartialAppl& node);

  // Formulas
  virtual void Visit(RelFormulaBool& node);
  virtual void Visit(RelFullAppl& node);
  virtual void Visit(RelQuantification& node);
  virtual void Visit(RelParen& node);
  virtual void Visit(RelComparison& node);
  virtual void Visit(RelUnOp& node);
  virtual void Visit(RelBinOp& node);

  // Terms
  virtual void Visit(RelIDTerm& node);
  virtual void Visit(RelNumTerm& node);
  virtual void Visit(RelOpTerm& node);
  virtual void Visit(RelParenthesisTerm& node);
};

}  // namespace rel2sql

#endif  // REL_AST_REL_AST_VISITOR_H
