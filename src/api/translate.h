#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "RelLexer.h"
#include "RelParser.h"
#include "optimizer/optimizer.h"
#include "parser/error_listener.h"
#include "preprocessing/preprocessor.h"
#include "rel_ast/rel_ast.h"
#include "rel_ast/relation_info.h"
#include "rel_ast/rel_context.h"
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

inline std::shared_ptr<sql::ast::Expression> GetSQLFromRelContainer(RelContext& container) {
  auto root = container.Root();
  if (!root) return nullptr;
  Translator visitor(&container);
  return visitor.Translate(*root);
}

/** Parse → PreprocessorRel → SQLVisitorRel → (optional) Optimizer */
inline std::shared_ptr<sql::ast::Expression> GetSQLRel(std::string_view input,
                                                       const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->program();
  Preprocessor preprocessor(edb_map);
  auto& container = preprocessor.Process(tree);
  auto sql = GetSQLFromRelContainer(container);
  if (!sql) return nullptr;
  sql::ast::Optimizer optimizer;
  optimizer.Visit(*sql);
  return sql;
}

inline std::shared_ptr<sql::ast::Expression> GetUnoptimizedSQLRel(
    std::string_view input, const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->program();
  Preprocessor preprocessor(edb_map);
  auto& container = preprocessor.Process(tree);
  return GetSQLFromRelContainer(container);
}

/** Rel pipeline: parse as formula → PreprocessorRel → SQLVisitorRel. No def wrapper, no DISTINCT. */
inline std::shared_ptr<sql::ast::Expression> GetSQLFromFormula(
    std::string_view input, const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->formula();
  Preprocessor preprocessor(edb_map);
  std::shared_ptr<RelFormula> formula;
  preprocessor.ProcessFormula(tree, formula);
  Translator visitor(preprocessor.GetContainer());
  return visitor.TranslateFormula(*formula);
}

/** Rel pipeline: parse as expr → PreprocessorRel → SQLVisitorRel. No def wrapper, no DISTINCT. */
inline std::shared_ptr<sql::ast::Expression> GetSQLFromExpr(
    std::string_view input, const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->expr();
  Preprocessor preprocessor(edb_map);
  std::shared_ptr<RelExpr> expr;
  preprocessor.ProcessExpr(tree, expr);
  Translator visitor(preprocessor.GetContainer());
  return visitor.TranslateExpr(*expr);
}

}  // namespace rel2sql

#endif  // TRANSLATE_H
