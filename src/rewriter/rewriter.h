#ifndef REWRITER_REWRITER_H
#define REWRITER_REWRITER_H

#include <memory>
#include <vector>

#include "rel_ast/rel_ast.h"
#include "rewriter/base_rewriter.h"
#include "rewriter/binding_domain_rewriter.h"

namespace rel2sql {

/**
 * Holds a sequence of rewriters and applies them to a Rel AST.
 * When given a root node, runs each rewriter in order; if a rewriter
 * replaces the root, the new node is used for the next rewriter.
 */
class Rewriter {
 public:
  Rewriter() { Add(std::make_unique<BindingDomainRewriter>()); };

  void Add(std::unique_ptr<BaseRelRewriter> rewriter) { rewriters_.push_back(std::move(rewriter)); }

  /** Runs all rewriters on the program (traverses defs and bodies). */
  void Run(std::shared_ptr<RelProgram> program);

  /**
   * Runs all rewriters on the expression. Returns the (possibly new) root.
   * If any rewriter replaces the root via SetExprReplacement, that becomes
   * the root for the next rewriter and the final return value.
   */
  std::shared_ptr<RelExpr> Run(std::shared_ptr<RelExpr> expr);

  /**
   * Runs all rewriters on the formula. Returns the (possibly new) root.
   */
  std::shared_ptr<RelFormula> Run(std::shared_ptr<RelFormula> formula);

  /**
   * Runs all rewriters on the abstraction. Returns the (possibly new) root.
   */
  std::shared_ptr<RelAbstraction> Run(std::shared_ptr<RelAbstraction> abstraction);

  bool empty() const { return rewriters_.empty(); }
  size_t size() const { return rewriters_.size(); }

 private:
  std::vector<std::unique_ptr<BaseRelRewriter>> rewriters_;
};

}  // namespace rel2sql

#endif  // REWRITER_REWRITER_H
