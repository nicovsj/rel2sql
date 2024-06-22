#ifndef EXTENDED_DATA_H
#define EXTENDED_DATA_H

#include <antlr4-runtime.h>

#include <set>
#include <string>

#include "sql.h"

struct ExtendedNode {
  // Variables are the variables that are bound in the current context
  std::set<std::string> variables;
  std::set<std::string> free_variables;

  // SQL expression is the SQL expression that corresponds to the current context
  std::shared_ptr<sql::ast::Expression> sql_expression;

  void InplaceUnion(const ExtendedNode &other) {
    variables.insert(other.variables.begin(), other.variables.end());
    free_variables.insert(other.free_variables.begin(), other.free_variables.end());
  }

  void InplaceDifference(const ExtendedNode &other) {
    ExtendedNode result;
    variables.insert(other.variables.begin(), other.variables.end());
    for (const auto &var : other.free_variables) {
      free_variables.erase(var);
    }
  }
};

class ExtendedAST {
 public:
  ExtendedAST(antlr4::ParserRuleContext *root,
              std::shared_ptr<std::unordered_map<antlr4::ParserRuleContext *, ExtendedNode>> extended_data)
      : root(root), extended_data(extended_data) {}

  ExtendedNode Root() const { return (*extended_data)[root]; }

  void Set(antlr4::ParserRuleContext *ctx, ExtendedNode data) { (*extended_data)[ctx] = data; }

  ExtendedNode Get(antlr4::ParserRuleContext *ctx) { return (*extended_data)[ctx]; }

 private:
  antlr4::ParserRuleContext *root;
  std::shared_ptr<std::unordered_map<antlr4::ParserRuleContext *, ExtendedNode>> extended_data;
};

#endif  // EXTENDED_DATA_H
