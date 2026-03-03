#ifndef PREPROCESSING_PREPROCESSOR_REL_H
#define PREPROCESSING_PREPROCESSOR_REL_H

#include <antlr4-runtime.h>

#include <memory>

#include "preprocessing/arity_visitor.h"
#include "preprocessing/balancing_visitor.h"
#include "preprocessing/ids_visitor.h"
#include "preprocessing/lit_visitor.h"
#include "preprocessing/recursion_visitor.h"
#include "preprocessing/safety_visitor.h"
#include "preprocessing/term_polynomial_visitor.h"
#include "preprocessing/vars_visitor.h"
#include "rel_ast/rel_ast_builder.h"
#include "rel_ast/rel_context.h"
#include "rel_ast/relation_info.h"
#include "rewriter/binding_domain_rewriter.h"
#include "rewriter/expression_as_term_rewriter.h"
#include "rewriter/underscore_rewriter.h"

namespace rel2sql {

/**
 * Preprocessor that builds typed RelAST from ANTLR and runs a unified pipeline
 * of visitors and rewriters. Rewriters run after IDsVisitor and ArityVisitor
 * (which populate the container) so that UnderscoreRewriter can use relation
 * arities for partial application.
 */
class Preprocessor {
 public:
  Preprocessor() : container_() {}

  explicit Preprocessor(const RelationMap& edb_map) : container_(edb_map) {}

  RelContext& Process(antlr4::ParserRuleContext* tree) {
    RelASTBuilder builder;
    auto program = builder.Build(tree);
    container_.SetRoot(program);
    std::shared_ptr<RelNode> root = program;
    root = RunPipeline(std::move(root));
    container_.SetRoot(std::dynamic_pointer_cast<RelProgram>(root));
    return container_;
  }

  void ProcessFormula(antlr4::ParserRuleContext* tree, std::shared_ptr<RelFormula>& formula_out) {
    RelASTBuilder builder;
    formula_out = builder.BuildFromFormula(tree);
    std::shared_ptr<RelNode> root = RunPipeline(std::shared_ptr<RelNode>(formula_out));
    formula_out = std::dynamic_pointer_cast<RelFormula>(root);
  }

  void ProcessExpr(antlr4::ParserRuleContext* tree, std::shared_ptr<RelExpr>& expr_out) {
    RelASTBuilder builder;
    expr_out = builder.BuildFromExpr(tree);
    std::shared_ptr<RelNode> root = RunPipeline(std::shared_ptr<RelNode>(expr_out));
    expr_out = std::dynamic_pointer_cast<RelExpr>(root);
  }

  RelContext* GetContainer() { return &container_; }
  const RelContext* GetContainer() const { return &container_; }

 private:
  std::shared_ptr<RelNode> RunPipeline(std::shared_ptr<RelNode> root) {
    BindingDomainRewriter binding_domain_rewriter;
    root->Accept(binding_domain_rewriter);
    if (auto r = binding_domain_rewriter.TakeExprReplacement()) root = r;
    if (auto r = binding_domain_rewriter.TakeFormulaReplacement()) root = r;

    ExpressionAsTermRewriter expr_as_term_rewriter;
    root->Accept(expr_as_term_rewriter);
    if (auto r = expr_as_term_rewriter.TakeExprReplacement()) root = r;
    if (auto r = expr_as_term_rewriter.TakeFormulaReplacement()) root = r;

    // Populate container (IDs, arities) first so rewriters like UnderscoreRewriter
    // can use GetArity for partial application.
    IDsVisitor ids_visitor(&container_);
    root->Accept(ids_visitor);

    ArityVisitor arity_visitor(&container_);
    root->Accept(arity_visitor);

    UnderscoreRewriter underscore_rewriter(GetContainer());
    root->Accept(underscore_rewriter);
    if (auto r = underscore_rewriter.TakeExprReplacement()) root = r;
    if (auto r = underscore_rewriter.TakeFormulaReplacement()) root = r;

    IDsVisitor ids_visitor2(&container_);
    root->Accept(ids_visitor2);

    ArityVisitor arity_visitor2(&container_);
    root->Accept(arity_visitor2);

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

    return root;
  }

  RelContext container_;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_PREPROCESSOR_REL_H
