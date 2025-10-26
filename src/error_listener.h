#ifndef ERROR_LISTENER_H
#define ERROR_LISTENER_H

#include <antlr4-runtime.h>

namespace rel2sql {

class Rel2SqlErrorListener : public antlr4::BaseErrorListener {
 public:
  Rel2SqlErrorListener() = default;
  virtual ~Rel2SqlErrorListener() = default;

  void syntaxError(antlr4::Recognizer* recognizer, antlr4::Token* offendingSymbol, size_t line,
                   size_t charPositionInLine, const std::string& msg, std::exception_ptr e) override;

 private:
  std::string extractCodeSnippet(antlr4::TokenStream* tokens, antlr4::Token* offendingSymbol, size_t line);
};

}  // namespace rel2sql

#endif  // ERROR_LISTENER_H
