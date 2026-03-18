#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "RelLexer.h"
#include "RelParser.h"
#include "optimizer/optimizer.h"
#include "parser/error_listener.h"
#include "rel_ast/rel_ast_builder.h"
#include "rel_ast/rel_context.h"
#include "rel_ast/rel_context_builder.h"
#include "rel_ast/relation_info.h"
#include "sql/translator.h"

namespace rel2sql {

inline std::unique_ptr<rel_parser::RelParser> GetParser(std::string_view input) {
  auto input_stream = new antlr4::ANTLRInputStream(input.data());
  auto lexer = new rel_parser::RelLexer(input_stream);
  auto tokens = new antlr4::CommonTokenStream(lexer);

  auto parser = std::make_unique<rel_parser::RelParser>(tokens);

  // Remove default error listeners and add our custom one
  parser->removeErrorListeners();
  parser->addErrorListener(new Rel2SqlErrorListener());

  return parser;
}

inline std::shared_ptr<sql::ast::Expression> GetSQLFromRelContext(const RelContext& context) {
  auto root = context.Root();

  if (!root) return nullptr;

  Translator translator(context);
  return translator.Translate();
}

/** Parse → RelASTBuilder → RelContextBuilder → SQLVisitorRel → (optional) Optimizer */
inline std::shared_ptr<sql::ast::Expression> GetSQLRel(std::string_view input,
                                                       const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->program();
  RelASTBuilder ast_builder;
  auto root = ast_builder.Build(tree);
  RelContextBuilder builder(edb_map);
  auto context = builder.Process(root);
  auto sql = GetSQLFromRelContext(context);
  if (!sql) return nullptr;
  sql::ast::Optimizer optimizer;
  return optimizer.Optimize(sql);
}

inline std::shared_ptr<sql::ast::Expression> GetUnoptimizedSQLRel(
    std::string_view input, const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->program();
  RelASTBuilder ast_builder;
  auto root = ast_builder.Build(tree);
  RelContextBuilder builder(edb_map);
  auto context = builder.Process(root);
  return GetSQLFromRelContext(context);
}

/** Rel pipeline: parse as formula → RelASTBuilder → RelContextBuilder → SQLVisitorRel. No def wrapper, no DISTINCT. */
inline std::shared_ptr<sql::ast::Expression> GetSQLFromFormula(
    std::string_view input, const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->formula();
  RelASTBuilder ast_builder;
  auto root = ast_builder.BuildFromFormula(tree);
  RelContextBuilder builder(edb_map);
  auto context = builder.Process(root);
  return GetSQLFromRelContext(context);
}

/** Rel pipeline: parse as expr → RelASTBuilder → RelContextBuilder → SQLVisitorRel. No def wrapper, no DISTINCT. */
inline std::shared_ptr<sql::ast::Expression> GetSQLFromExpr(
    std::string_view input, const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->expr();
  RelASTBuilder ast_builder;
  auto root = ast_builder.BuildFromExpr(tree);
  RelContextBuilder builder(edb_map);
  auto context = builder.Process(root);
  return GetSQLFromRelContext(context);
}

}  // namespace rel2sql

#endif  // TRANSLATE_H
