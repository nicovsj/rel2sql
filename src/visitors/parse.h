#ifndef PARSE_H
#define PARSE_H

#include "CoreRelLexer.h"
#include "PrunedCoreRelParser.h"
#include "sql_ast/opt_visitor.h"
#include "visitors/arity_visitor.h"
#include "visitors/balancing_visitor.h"
#include "visitors/ids_visitor.h"
#include "visitors/lit_visitor.h"
#include "visitors/safe_visitor.h"
#include "visitors/sql_visitor.h"
#include "visitors/vars_visitor.h"

namespace rel_parser {

inline std::unique_ptr<PrunedCoreRelParser> GetParser(std::string_view input) {
  auto input_stream = new antlr4::ANTLRInputStream(input.data());
  auto lexer = new CoreRelLexer(input_stream);
  auto tokens = new antlr4::CommonTokenStream(lexer);

  return std::make_unique<PrunedCoreRelParser>(tokens);
}

inline ExtendedAST GetExtendedASTFromTree(
    antlr4::ParserRuleContext* tree,
    std::shared_ptr<ExtendedASTData> extended_ast_data = std::make_shared<ExtendedASTData>()) {
  IDsVisitor ids_visitor(extended_ast_data);

  ArityVisitor arity_visitor(extended_ast_data);

  VariablesVisitor free_vars_visitor(extended_ast_data);

  LiteralVisitor literal_visitor(extended_ast_data);

  BalancingVisitor balancing_visitor(extended_ast_data);

  SafeVisitor safeness_visitor(extended_ast_data);

  ids_visitor.visit(tree);

  arity_visitor.visit(tree);

  free_vars_visitor.visit(tree);

  literal_visitor.visit(tree);

  balancing_visitor.visit(tree);

  safeness_visitor.visit(tree);

  return ExtendedAST{tree, extended_ast_data};
}

inline ExtendedAST GetExtendedAST(std::string_view input) {
  auto parser = GetParser(input);

  auto tree = parser->program();

  return GetExtendedASTFromTree(tree);
};

inline ExtendedAST GetExtendedAST(std::string_view input, std::unordered_map<std::string, int> extended_arity_map) {
  auto parser = GetParser(input);

  auto tree = parser->program();

  auto extended_ast_data = std::make_shared<ExtendedASTData>(extended_arity_map);

  return GetExtendedASTFromTree(tree, extended_ast_data);
};

inline std::shared_ptr<sql::ast::Expression> GetSQLFromTree(antlr4::ParserRuleContext* tree,
                                                            std::optional<ExtendedAST> ast = std::nullopt) {
  if (!ast) {
    ast = GetExtendedASTFromTree(tree);
  }

  SQLVisitor visitor(ast.value().Data());

  auto sql = std::any_cast<std::shared_ptr<sql::ast::Expression>>(visitor.visit(tree));

  return sql;
}

inline std::shared_ptr<sql::ast::Expression> GetSQL(std::string_view input) {
  auto parser = GetParser(input);

  auto tree = parser->program();

  auto ast = GetExtendedASTFromTree(tree);

  auto sql = GetSQLFromTree(tree);

  sql::ast::Optimizer optimizer;

  optimizer.Visit(*sql);

  return sql;
}

inline std::shared_ptr<sql::ast::Expression> GetUnoptimizedSQL(std::string_view input) {
  auto parser = GetParser(input);

  auto tree = parser->program();

  return GetSQLFromTree(tree);
}

}  // namespace rel_parser

#endif  // PARSE_H
