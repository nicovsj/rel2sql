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

  std::optional<sql::ast::constant_t> constant;

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

using ExtendedASTIndex = std::unordered_map<antlr4::ParserRuleContext *, ExtendedNode>;

class ExtendedAST {
 public:
  ExtendedAST(antlr4::ParserRuleContext *root, std::shared_ptr<ExtendedASTIndex> data) : root_(root), data_(data) {}

  ExtendedNode Root() const { return (*data_)[root_]; }

  void Set(antlr4::ParserRuleContext *ctx, ExtendedNode data) { (*data_)[ctx] = data; }

  ExtendedNode &Get(antlr4::ParserRuleContext *ctx) {
    auto it = data_->find(ctx);
    if (it == data_->end()) {
      (*data_)[ctx] = ExtendedNode{};
    }
    return it->second;
  }

 private:
  antlr4::ParserRuleContext *root_;
  std::shared_ptr<ExtendedASTIndex> data_;
};

#endif  // EXTENDED_DATA_H
