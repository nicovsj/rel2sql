// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "parser/parse.h"
#include "sql.h"

std::string TranslateRelFormula(const std::string& input) {
  /*
   * This function takes a string CoreRel formula input and returns the SQL translation.
   */
  auto parser = rel_parser::GetParser(input);
  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::FormulaContext*>(parser->formula());
  auto ast = rel_parser::GetExtendedASTFromTree(tree);
  auto result = rel_parser::GetSQLFromTree(tree);
  std::ostringstream os;
  os << *result;
  return os.str();
}

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
  EXPECT_EQ(TranslateRelFormula("F(x)"), "SELECT T0.A1 AS x FROM F AS T0");
}

TEST(TranslationTest, DoubleFullApplication) {
  EXPECT_EQ(TranslateRelFormula("F(x,y)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0");
}

TEST(TranslationTest, CompositionFormula) {
  EXPECT_EQ(TranslateRelFormula("F(G(x))"), "SELECT T2.x FROM F AS T0, (SELECT T1.A1 AS x FROM G AS T1) AS T2");
}

TEST(TranslationTest, ConjunctionFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x) and G(x)"),
            "SELECT T2.x FROM (SELECT T0.A1 AS x FROM F AS T0) AS T2, (SELECT T1.A1 AS x FROM G AS T1) AS T3 WHERE "
            "T2.x = T3.x");
}

TEST(TranslationTest, DisjunctionFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x) or G(x)"),
            "SELECT T2.x FROM (SELECT T0.A1 AS x FROM F AS T0) AS T2 UNION SELECT T3.x FROM (SELECT T1.A1 AS x FROM G "
            "AS T1) AS T3");
}
