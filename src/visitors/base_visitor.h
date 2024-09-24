#ifndef ABS_VISITOR_H
#define ABS_VISITOR_H

#include <antlr4-runtime.h>

#include "PrunedCoreRelParserBaseVisitor.h"
#include "parser/extended_ast.h"

class BaseVisitor : public rel_parser::PrunedCoreRelParserBaseVisitor {
  /*
   * Provides a base visitor for constructing an extended AST.
   */
 public:
  using psr = rel_parser::PrunedCoreRelParser;

  BaseVisitor() : ast_data_(std::make_shared<ExtendedASTData>()) {}

  BaseVisitor(std::shared_ptr<ExtendedASTData> ast_data) : ast_data_(ast_data) {}

 protected:
  ExtendedNode& GetNode(antlr4::ParserRuleContext* ctx) { return ast_data_->index[ctx]; }

  const ExtendedNode& GetNode(antlr4::ParserRuleContext* ctx) const { return ast_data_->index.at(ctx); }

  std::shared_ptr<ExtendedASTData> ast_data_;
};

#endif  // ABS_VISITOR_H
