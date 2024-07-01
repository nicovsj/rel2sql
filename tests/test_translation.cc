// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "parser/parse.h"
#include "sql.h"

TEST(SQLVisitorTest, EqualitySpecialCondition) {
  std::string input = "F(x) and G(x)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::BinOpContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto table_F = std::make_shared<sql::ast::Source>(std::make_shared<sql::ast::Table>("F"));
  auto table_G = std::make_shared<sql::ast::Source>(std::make_shared<sql::ast::Table>("G"));

  ast.Get(tree->lhs).sql_expression = table_F;
  ast.Get(tree->rhs).sql_expression = table_G;

  auto visitor = SQLVisitor(ast.Data());

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

  auto table_F = std::make_shared<sql::ast::Source>(std::make_shared<sql::ast::Table>("F"));
  auto table_G = std::make_shared<sql::ast::Source>(std::make_shared<sql::ast::Table>("G"));

  ast.Get(tree->lhs).sql_expression = table_F;
  ast.Get(tree->rhs).sql_expression = table_G;

  auto visitor = SQLVisitor(ast.Data());

  auto var_list = visitor.SpecialVarList(std::vector<antlr4::ParserRuleContext*>{tree->lhs, tree->rhs});

  std::ostringstream os;

  for (auto& col : var_list) {
    os << *col << " ";
  }

  EXPECT_EQ(os.str(), "F.x G.y ");
}

TEST(TranslationTest, FullApplicationFormula) {
  std::string input = "F(x)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::FormulaContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto result = rel_parser::GetSQLFromTree(tree);

  std::ostringstream os;

  os << *result;

  EXPECT_EQ(os.str(), "SELECT T0.A1 AS x FROM F AS T0");
}

TEST(TranslationTest, DoubleFullApplication) {
  std::string input = "F(x,y)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::FormulaContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto result = rel_parser::GetSQLFromTree(tree);

  std::ostringstream os;

  os << *result;

  EXPECT_EQ(os.str(), "SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0");
}

TEST(TranslationTest, ConjunctionFormula) {
  std::string input = "F(x) and G(x)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::FormulaContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto result = rel_parser::GetSQLFromTree(tree);

  std::ostringstream os;

  os << *result;

  EXPECT_EQ(os.str(),
            "SELECT T2.x FROM (SELECT T0.A1 AS x FROM F AS T0) AS T2, (SELECT T1.A1 AS x FROM G AS T1) AS T3 WHERE "
            "T2.x = T3.x");
}

TEST(TranslationTest, DisjunctionFormula) {
  std::string input = "F(x) or G(x)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::FormulaContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto result = rel_parser::GetSQLFromTree(tree);

  std::ostringstream os;

  os << *result;

  EXPECT_EQ(os.str(),
            "SELECT T2.x FROM (SELECT T0.A1 AS x FROM F AS T0) AS T2 UNION SELECT T3.x FROM (SELECT T1.A1 AS x FROM G "
            "AS T1) AS T3");
}

TEST(TranslationTest, CompositionFormula) {
  std::string input = "F(G(x))";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::FormulaContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto result = rel_parser::GetSQLFromTree(tree);

  std::ostringstream os;

  os << *result;

  EXPECT_EQ(os.str(), "SELECT T2.x FROM F AS T0, (SELECT T1.A1 AS x FROM G AS T1) AS T2");
}
