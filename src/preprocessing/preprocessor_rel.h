#ifndef PREPROCESSING_PREPROCESSOR_REL_H
#define PREPROCESSING_PREPROCESSOR_REL_H

#include <antlr4-runtime.h>

#include "preprocessing/arity_visitor_rel.h"
#include "preprocessing/balancing_visitor_rel.h"
#include "preprocessing/ids_visitor_rel.h"
#include "preprocessing/lit_visitor_rel.h"
#include "preprocessing/recursion_visitor_rel.h"
#include "preprocessing/safe_visitor_rel.h"
#include "preprocessing/term_polynomial_visitor_rel.h"
#include "preprocessing/vars_visitor_rel.h"
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
class PreprocessorRel {
 public:
  PreprocessorRel() : container_() {}

  explicit PreprocessorRel(const RelationMap& edb_map) : container_(edb_map) {}

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
    IDsVisitorRel ids_visitor(&container_);
    root->Accept(ids_visitor);

    ArityVisitorRel arity_visitor(&container_);
    root->Accept(arity_visitor);

    VariablesVisitorRel vars_visitor(&container_);
    root->Accept(vars_visitor);

    LiteralVisitorRel lit_visitor;
    root->Accept(lit_visitor);

    TermPolynomialVisitorRel term_poly_visitor;
    root->Accept(term_poly_visitor);

    RecursionVisitorRel recursion_visitor(&container_);
    root->Accept(recursion_visitor);

    BalancingVisitorRel balancing_visitor;
    root->Accept(balancing_visitor);

    SafeVisitorRel safe_visitor(&container_);
    root->Accept(safe_visitor);
  }

  RelASTContainer container_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_PREPROCESSOR_REL_H
