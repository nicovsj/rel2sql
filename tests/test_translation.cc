// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "parser/parse.h"
#include "sql.h"

TEST(SQLVisitorTest, EqualitySpecialCondition) {
  std::string input = "F(x) and G(x)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::BinOpContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto table_F = std::make_shared<sql::ast::Table>("F");
  auto table_G = std::make_shared<sql::ast::Table>("G");

  ast.Get(tree->lhs).sql_expression = table_F;
  ast.Get(tree->rhs).sql_expression = table_G;

  auto visitor = SQLVisitor(ast);

  auto condition = visitor.EqualitySpecialCondition(std::vector<antlr4::ParserRuleContext*>{tree->lhs, tree->rhs});

  std::ostringstream os;

  os << *condition;

  EXPECT_EQ(os.str(), "F.x = G.x");
}

TEST(SQLVisitorTest, SpecialVarList) {
  std::string input = "F(x) and G(x, y)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::BinOpContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto table_F = std::make_shared<sql::ast::Table>("F");
  auto table_G = std::make_shared<sql::ast::Table>("G");

  ast.Get(tree->lhs).sql_expression = table_F;
  ast.Get(tree->rhs).sql_expression = table_G;

  auto visitor = SQLVisitor(ast);

  auto var_list = visitor.SpecialVarList(std::vector<antlr4::ParserRuleContext*>{tree->lhs, tree->rhs});

  std::ostringstream os;

  for (auto& col : var_list) {
    os << *col << " ";
  }

  EXPECT_EQ(os.str(), "F.x G.y ");
}

TEST(TranslationTest, ConjunctionExpr) {
  GTEST_SKIP() << "Not implemented yet";

  std::string input = "F(x) and G(x, y)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::FormulaContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto result = rel_parser::GetSQLFromTree(tree);

  std::ostringstream os;

  os << *result;

  EXPECT_EQ(os.str(), "SELECT F.x FROM F WHERE G.x = F.x");
}
