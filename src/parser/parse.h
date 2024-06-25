#ifndef PARSE_H
#define PARSE_H

#include "parser/generated/CoreRelLexer.h"
#include "parser/generated/PrunedCoreRelParser.h"
#include "parser/visitors/fv_visitor.h"
#include "parser/visitors/lit_visitor.h"
#include "parser/visitors/sql_visitor.h"

namespace rel_parser {

inline std::unique_ptr<PrunedCoreRelParser> GetParser(std::string_view input) {
  auto input_stream = new antlr4::ANTLRInputStream(input.data());
  auto lexer = new CoreRelLexer(input_stream);
  auto tokens = new antlr4::CommonTokenStream(lexer);

  return std::make_unique<PrunedCoreRelParser>(tokens);
}

inline ExtendedAST GetExtendedASTFromTree(antlr4::ParserRuleContext* tree) {
  auto index = std::make_shared<ExtendedASTIndex>();

  FreeVariablesVisitor free_vars_visitor(index);

  LiteralVisitor literal_visitor(index);

  free_vars_visitor.visit(tree);

  return std::any_cast<ExtendedAST>(literal_visitor.visit(tree));
}

inline ExtendedAST GetExtendedAST(std::string_view input) {
  auto parser = GetParser(input);

  auto tree = parser->program();

  return GetExtendedASTFromTree(tree);
};

inline std::shared_ptr<sql::ast::Expression> GetSQLFromTree(antlr4::ParserRuleContext* tree) {
  ExtendedAST ast = GetExtendedASTFromTree(tree);

  SQLVisitor visitor(ast);

  return std::any_cast<std::shared_ptr<sql::ast::SelectStatement>>(visitor.visit(tree));
}

inline std::shared_ptr<sql::ast::Expression> GetSQL(std::string_view input) {
  auto parser = GetParser(input);

  auto tree = parser->formula();

  auto ast = GetExtendedASTFromTree(tree);

  return GetSQLFromTree(tree);
}

}  // namespace rel_parser

#endif  // PARSE_H
