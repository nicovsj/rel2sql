#ifndef PREPROCESSING_LIT_VISITOR_REL_H
#define PREPROCESSING_LIT_VISITOR_REL_H

#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_ast_visitor.h"

namespace rel2sql {

class LiteralVisitor : public RelASTVisitor {
 public:
  LiteralVisitor() = default;

  void Visit(RelProgram& node) override;
  void Visit(RelDef& node) override;
  void Visit(RelAbstraction& node) override;
  void Visit(RelLiteral& node) override;
  void Visit(RelLitExpr& node) override;
  void Visit(RelProductExpr& node) override;
  void Visit(RelAbstractionExpr& node) override;
  void Visit(RelNumTerm& node) override;
};

}  // namespace rel2sql

#endif  // PREPROCESSING_LIT_VISITOR_REL_H
