#include "base_visitor.h"

namespace rel2sql {

SourceLocation BaseVisitor::GetSourceLocation(antlr4::ParserRuleContext* ctx) {
  if (!ctx) {
    return SourceLocation(0, 0);
  }

  // Get line and column from the context
  int line = ctx->getStart() ? ctx->getStart()->getLine() : 0;
  int column = ctx->getStart() ? ctx->getStart()->getCharPositionInLine() : 0;

  // Extract text snippet from the context
  std::string text_snippet = ctx->getText();

  // Limit snippet length for readability
  if (text_snippet.length() > 100) {
    text_snippet = text_snippet.substr(0, 97) + "...";
  }

  return SourceLocation(line, column, text_snippet);
}

}  // namespace rel2sql
