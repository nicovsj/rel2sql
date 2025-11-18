#ifndef ABS_VISITOR_H
#define ABS_VISITOR_H

#include <antlr4-runtime.h>

#include "PrunedCoreRelParserBaseVisitor.h"
#include "structs/extended_ast.h"
#include "utils/exceptions.h"

namespace rel2sql {

class BaseVisitor : public rel_parser::PrunedCoreRelParserBaseVisitor {
  /*
   * Provides a base visitor for constructing an extended AST.
   */
 public:
  using psr = rel_parser::PrunedCoreRelParser;

  BaseVisitor() : ast_(std::make_shared<RelAST>()) {}

  BaseVisitor(std::shared_ptr<RelAST> ast) : ast_(ast) {}

 protected:
  std::shared_ptr<RelASTNode> GetNode(antlr4::ParserRuleContext* ctx) { return ast_->GetNode(ctx); }
  std::shared_ptr<RelASTNode> GetNode(antlr4::ParserRuleContext* ctx) const { return ast_->GetNode(ctx); }

  // Helper method to extract source location from ANTLR context
  rel2sql::SourceLocation GetSourceLocation(antlr4::ParserRuleContext* ctx);

  std::shared_ptr<RelAST> ast_;
};

}  // namespace rel2sql

#endif  // ABS_VISITOR_H
