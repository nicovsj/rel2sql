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

  auto condition = visitor.EqualityShorthand(std::vector<antlr4::ParserRuleContext*>{tree->lhs, tree->rhs});

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

  auto var_list = visitor.VarListShorthand(std::vector<antlr4::ParserRuleContext*>{tree->lhs, tree->rhs});

  std::ostringstream os;

  for (auto& col : var_list) {
    os << *col << " ";
  }

  EXPECT_EQ(os.str(), "F.x G.y ");
}

TEST(TranslationTest, FullApplicationFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x)"), "SELECT T0.A1 AS x FROM F AS T0");
}

TEST(TranslationTest, FullApplicationFormulaMultipleParams) {
  EXPECT_EQ(TranslateRelFormula("F(x, y)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0");
  EXPECT_EQ(TranslateRelFormula("F(x, y, z)"), "SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM F AS T0");
}

TEST(TranslationTest, NestedFullApplicationFormula) {
  EXPECT_EQ(TranslateRelFormula("F(G(x))"), "SELECT T2.x FROM F AS T0, (SELECT T1.A1 AS x FROM G AS T1) AS T2");
  EXPECT_EQ(TranslateRelFormula("F(G(H(x)))"),
            "SELECT T4.x FROM F AS T0, (SELECT T3.x FROM G AS T1, (SELECT T2.A1 AS x FROM H AS T2) AS T3) AS T4");
}

TEST(TranslationTest, FullApplicationFormulaMultipleMixedParams) {
  EXPECT_EQ(TranslateRelFormula("F(G(x), y)"),
            "SELECT T2.x, T0.A2 AS y FROM F AS T0, (SELECT T1.A1 AS x FROM G AS T1) AS T2");
  EXPECT_EQ(TranslateRelFormula("F(x, G(y))"),
            "SELECT T0.A1 AS x, T2.y FROM F AS T0, (SELECT T1.A1 AS y FROM G AS T1) AS T2");
  EXPECT_EQ(
      TranslateRelFormula("F(G(x), H(y))"),
      "SELECT T2.x, T4.y FROM F AS T0, (SELECT T1.A1 AS x FROM G AS T1) AS T2, (SELECT T3.A1 AS y FROM H AS T3) AS T4");
}

TEST(TranslationTest, FullApplicationFormulaMultipleParamsSharingVariables) {
  EXPECT_EQ(TranslateRelFormula("F(G(x), H(x))"),
            "SELECT T2.x FROM F AS T0, (SELECT T1.A1 AS x FROM G AS T1) AS T2, (SELECT T3.A1 AS x FROM H AS T3) AS T4 "
            "WHERE T2.x = T4.x");
  EXPECT_EQ(TranslateRelFormula("F(G(x, y), H(y, z))"),
            "SELECT T2.x, T2.y, T4.z FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS y FROM G AS T1) AS T2, (SELECT T3.A1 "
            "AS y, T3.A2 AS z FROM H AS T3) AS T4 WHERE T2.y = T4.y");

  EXPECT_EQ(TranslateRelFormula("F(G(x), x)"),
            "SELECT T2.x FROM F AS T0, (SELECT T1.A1 AS x FROM G AS T1) AS T2 WHERE T2.x = T0.A2");

  EXPECT_EQ(TranslateRelFormula("F(G(x), x, y)"),
            "SELECT T2.x, T0.A3 AS y FROM F AS T0, (SELECT T1.A1 AS x FROM G AS T1) AS T2 WHERE T2.x = T0.A2");
}

TEST(TranslationTest, RepeatedVariableFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x, x)"), "SELECT T0.A1 AS x FROM F AS T0 WHERE T0.A1 = T0.A2");
  EXPECT_EQ(TranslateRelFormula("F(x, x, x)"), "SELECT T0.A1 AS x FROM F AS T0 WHERE T0.A1 = T0.A2 AND T0.A2 = T0.A3");
  EXPECT_EQ(TranslateRelFormula("F(x, y, x)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0 WHERE T0.A1 = T0.A3");
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

TEST(TranslationTest, ExistentialFormula) {
  EXPECT_EQ(TranslateRelFormula("exists (y | F(x, y))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1");
  EXPECT_EQ(TranslateRelFormula("exists (y, z | F(x, y, z))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM F AS T0) AS T1");

  EXPECT_EQ(TranslateRelFormula("exists (y in G | F(x, y))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1, G WHERE T1.y = G.A1");
  EXPECT_EQ(TranslateRelFormula("exists (y in G, z in H | F(x, y, z))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM F AS T0) AS T1, G, H WHERE T1.y = G.A1 "
            "AND T1.z = H.A1");
  EXPECT_EQ(TranslateRelFormula("exists (y in G, z | F(x, y, z))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM F AS T0) AS T1, G WHERE T1.y = G.A1");
}

TEST(TranslationTest, UniversalFormula) {
  // TODO: Must remove inner-most FROM subquery alias (final "AS T1")
  EXPECT_EQ(TranslateRelFormula("forall (y in G | F(x, y))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1 WHERE EXISTS (SELECT * FROM G WHERE "
            "(T1.x, G.A1) NOT IN (SELECT * FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1))");

  EXPECT_EQ(TranslateRelFormula("forall (y in G, z in H | F(x, y, z))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM F AS T0) AS T1 WHERE EXISTS (SELECT * "
            "FROM G, H WHERE (T1.x, G.A1, H.A1) NOT IN (SELECT * FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM "
            "F AS T0) AS T1))");
}
