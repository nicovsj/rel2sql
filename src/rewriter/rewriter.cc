#include "rewriter/rewriter.h"

namespace rel2sql {

void Rewriter::Run(std::shared_ptr<RelProgram> program) {
  for (auto& r : rewriters_) {
    program->Accept(*r);
  }
}

std::shared_ptr<RelExpr> Rewriter::Run(std::shared_ptr<RelExpr> expr) {
  if (!expr) return expr;
  for (auto& r : rewriters_) {
    expr->Accept(*r);
    if (auto replacement = r->TakeExprReplacement()) {
      expr = replacement;
    }
  }
  return expr;
}

std::shared_ptr<RelFormula> Rewriter::Run(std::shared_ptr<RelFormula> formula) {
  if (!formula) return formula;
  for (auto& r : rewriters_) {
    formula->Accept(*r);
    if (auto replacement = r->TakeFormulaReplacement()) {
      formula = replacement;
    }
  }
  return formula;
}

std::shared_ptr<RelAbstraction> Rewriter::Run(std::shared_ptr<RelAbstraction> abstraction) {
  if (!abstraction) return abstraction;
  for (auto& r : rewriters_) {
    abstraction->Accept(*r);
    if (auto replacement = r->TakeAbstractionReplacement()) {
      abstraction = replacement;
    }
  }
  return abstraction;
}

}  // namespace rel2sql
