#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "CoreRelLexer.h"
#include "PrunedCoreRelParser.h"
#include "optimizer/optimizer.h"
#include "parser/error_listener.h"
#include "preprocessing/preprocessor.h"
#include "rel_ast/edb_info.h"
#include "sql/sql_visitor.h"

namespace rel2sql {

inline std::unique_ptr<rel_parser::PrunedCoreRelParser> GetParser(std::string_view input) {
  auto input_stream = new antlr4::ANTLRInputStream(input.data());
  auto lexer = new rel_parser::CoreRelLexer(input_stream);
  auto tokens = new antlr4::CommonTokenStream(lexer);

  auto parser = std::make_unique<rel_parser::PrunedCoreRelParser>(tokens);

  // Remove default error listeners and add our custom one
  parser->removeErrorListeners();
  parser->addErrorListener(new Rel2SqlErrorListener());

  return parser;
}

inline std::shared_ptr<sql::ast::Expression> GetSQLFromAST(const RelAST& ast) {
  // Create a shared_ptr to pass to SQLVisitor (visitors need shared_ptr for BaseVisitor)
  // Note: This shared_ptr doesn't own the ExtendedAST, but that's okay since it's used temporarily
  auto ast_ptr = std::shared_ptr<RelAST>(const_cast<RelAST*>(&ast), [](RelAST*) {});
  SQLVisitor visitor(ast_ptr);

  auto sql = visitor.visit(ast.ParseTree());

  return std::any_cast<std::shared_ptr<sql::ast::Expression>>(sql);
}

inline std::shared_ptr<sql::ast::Expression> GetSQL(std::string_view input,
                                                    const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto parse_tree = parser->program();

  Preprocessor preprocessor(edb_map);
  auto ast = preprocessor.Process(parse_tree);

  auto sql = GetSQLFromAST(ast);

  sql::ast::Optimizer optimizer;
  optimizer.Visit(*sql);

  return sql;
}

inline std::shared_ptr<sql::ast::Expression> GetUnoptimizedSQL(std::string_view input,
                                                               const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);

  auto tree = parser->program();

  Preprocessor preprocessor(edb_map);
  auto ast = preprocessor.Process(tree);

  return GetSQLFromAST(ast);
}

}  // namespace rel2sql

#endif  // TRANSLATE_H
