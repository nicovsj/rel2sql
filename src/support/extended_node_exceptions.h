#ifndef EXTENDED_NODE_EXCEPTIONS_H
#define EXTENDED_NODE_EXCEPTIONS_H

#include <antlr4-runtime.h>

#include <stdexcept>
#include <string>

namespace rel2sql {

class ExtendedNodeDifferenceException : public std::runtime_error {
 public:
  ExtendedNodeDifferenceException(antlr4::ParserRuleContext* differing_context, const std::string& field_name,
                                  const std::string& details)
      : std::runtime_error(BuildMessage(differing_context, field_name, details)),
        differing_context_(differing_context),
        field_name_(field_name),
        details_(details) {}

  antlr4::ParserRuleContext* GetDifferingContext() const { return differing_context_; }
  const std::string& GetFieldName() const { return field_name_; }
  const std::string& GetDetails() const { return details_; }

 private:
  static std::string BuildMessage(antlr4::ParserRuleContext* ctx, const std::string& field_name,
                                  const std::string& details) {
    std::string msg = "ExtendedNode difference found";
    if (ctx) {
      msg += " at context: " + ctx->getText();
    }
    msg += " in field: " + field_name;
    if (!details.empty()) {
      msg += " (" + details + ")";
    }
    return msg;
  }

  antlr4::ParserRuleContext* differing_context_;
  std::string field_name_;
  std::string details_;
};

}  // namespace rel2sql

#endif  // EXTENDED_NODE_EXCEPTIONS_H
