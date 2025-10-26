#include "error_listener.h"

#include <sstream>

namespace rel2sql {

void Rel2SqlErrorListener::syntaxError(antlr4::Recognizer* recognizer, antlr4::Token* offendingSymbol, size_t line,
                                       size_t charPositionInLine, const std::string& msg, std::exception_ptr e) {
  // Extract code snippet for better error reporting
  std::string code_snippet;
  if (auto* tokenStream = dynamic_cast<antlr4::TokenStream*>(recognizer->getInputStream())) {
    code_snippet = extractCodeSnippet(tokenStream, offendingSymbol, line);
  }

  // Create source location
  SourceLocation location(static_cast<int>(line), static_cast<int>(charPositionInLine), code_snippet);

  // Create formatted error message
  std::ostringstream error_msg;
  error_msg << "Syntax error: " << msg;

  if (offendingSymbol && offendingSymbol->getType() != antlr4::Token::EOF) {
    error_msg << " (unexpected token: '" << offendingSymbol->getText() << "')";
  }

  // Throw ParseException with location information
  throw ParseException(error_msg.str(), location);
}

std::string Rel2SqlErrorListener::extractCodeSnippet(antlr4::TokenStream* tokens, antlr4::Token* offendingSymbol,
                                                     size_t line) {
  if (!tokens || !offendingSymbol) {
    return "";
  }

  std::ostringstream snippet;

  // Get tokens around the error line
  size_t start_line = (line > 2) ? line - 2 : 1;
  size_t end_line = line + 2;

  // Find tokens in the specified line range
  std::vector<antlr4::Token*> line_tokens;
  for (size_t i = 0; i < tokens->size(); ++i) {
    auto* token = tokens->get(i);
    if (token->getLine() >= start_line && token->getLine() <= end_line) {
      line_tokens.push_back(token);
    }
  }

  // Build snippet from tokens
  size_t current_line = 0;
  for (auto* token : line_tokens) {
    if (token->getLine() != current_line) {
      if (current_line != 0) {
        snippet << "\n";
      }
      current_line = token->getLine();
    }

    if (token->getType() != antlr4::Token::EOF) {
      snippet << token->getText() << " ";
    }
  }

  return snippet.str();
}

}  // namespace rel2sql
