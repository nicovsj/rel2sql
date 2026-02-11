#ifndef PREPROCESSING_PREPROCESSOR_REL_H
#define PREPROCESSING_PREPROCESSOR_REL_H

#include <antlr4-runtime.h>

#include "preprocessing/arity_visitor.h"
#include "preprocessing/balancing_visitor.h"
#include "preprocessing/ids_visitor.h"
#include "preprocessing/lit_visitor.h"
#include "preprocessing/recursion_visitor.h"
#include "preprocessing/safety_visitor.h"
#include "preprocessing/term_polynomial_visitor.h"
#include "preprocessing/vars_visitor.h"
#include "rel_ast/rel_ast_builder.h"
#include "rel_ast/rel_ast_container.h"
#include "rel_ast/relation_info.h"
#include "rewriter/rewriter.h"

namespace rel2sql {

/**
 * Preprocessor that uses the RelAST pipeline: builds typed RelAST from ANTLR
 * and runs Rel-based visitors (IDs, Arity, Variables).
 *
 * This is the new pipeline per the Rel AST Migration Plan. Additional visitors
 * (Recursion, Literal, TermPolynomial, Balancing, Safe) will be migrated next.
 */
class Preprocessor {
 public:
  Preprocessor() : container_() {}

  explicit Preprocessor(const RelationMap& edb_map) : container_(edb_map) {}

  RelASTContainer& Process(antlr4::ParserRuleContext* tree) {
    RelASTBuilder builder;
    auto program = builder.Build(tree);
    Rewriter rewriter;
    rewriter.Run(program);
    container_.SetRoot(program);
    RunVisitorsOnRoot(program);
    return container_;
  }

  void ProcessFormula(antlr4::ParserRuleContext* tree, std::shared_ptr<RelFormula>& formula_out) {
    RelASTBuilder builder;
    formula_out = builder.BuildFromFormula(tree);
    Rewriter rewriter;
    formula_out = rewriter.Run(formula_out);
    RunVisitorsOnRoot(formula_out);
  }

  void ProcessExpr(antlr4::ParserRuleContext* tree, std::shared_ptr<RelExpr>& expr_out) {
    RelASTBuilder builder;
    expr_out = builder.BuildFromExpr(tree);
    Rewriter rewriter;
    expr_out = rewriter.Run(expr_out);
    RunVisitorsOnRoot(expr_out);
  }

  RelASTContainer* GetContainer() { return &container_; }
  const RelASTContainer* GetContainer() const { return &container_; }

 private:
  void RunVisitorsOnRoot(std::shared_ptr<RelNode> root) {
    IDsVisitor ids_visitor(&container_);
    root->Accept(ids_visitor);

    ArityVisitor arity_visitor(&container_);
    root->Accept(arity_visitor);

    VariablesVisitor vars_visitor(&container_);
    root->Accept(vars_visitor);

    LiteralVisitor lit_visitor;
    root->Accept(lit_visitor);

    TermPolynomialVisitor term_poly_visitor;
    root->Accept(term_poly_visitor);

    RecursionVisitor recursion_visitor(&container_);
    root->Accept(recursion_visitor);

    BalancingVisitor balancing_visitor;
    root->Accept(balancing_visitor);

    SafetyVisitor safe_visitor(&container_);
    root->Accept(safe_visitor);
  }

  RelASTContainer container_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_PREPROCESSOR_REL_H
