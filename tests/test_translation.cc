// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "sql_ast/sql_ast.h"
#include "visitors/parse.h"

std::string TranslateRelProgram(const std::string& input,
                                std::unordered_map<std::string, int> external_arity_map = {}) {
  /*
   * This function takes a string CoreRel program input and returns the SQL translation.
   */
  auto parser = rel_parser::GetParser(input);
  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::RelDefContext*>(parser->relDef());
  auto ast_data = std::make_shared<ExtendedASTData>(external_arity_map);
  auto ast = rel_parser::GetExtendedASTFromTree(tree, ast_data);
  auto result = rel_parser::GetSQLFromTree(tree, ast);
  std::ostringstream os;
  os << *result;
  return os.str();
}

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

std::string TranslateRelExpression(const std::string& input,
                                   std::unordered_map<std::string, int> external_arity_map = {}) {
  /*
   * This function takes a string CoreRel expression input and returns the SQL translation.
   */
  auto parser = rel_parser::GetParser(input);
  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::ExprContext*>(parser->expr());
  auto ast_data = std::make_shared<ExtendedASTData>(external_arity_map);
  auto ast = rel_parser::GetExtendedASTFromTree(tree, ast_data);
  auto result = rel_parser::GetSQLFromTree(tree, ast);
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

TEST(TranslationTest, ProductExpression) {
  EXPECT_EQ(TranslateRelExpression("(1, 2)"),
            "SELECT T0.A1, T1.A1 FROM (SELECT 1 AS A1) AS T0, (SELECT 2 AS A1) AS T1");
}

TEST(TranslationTest, ConditionExpression) {
  EXPECT_EQ(TranslateRelExpression("F[x] where G(x)", {{"F", 2}, {"G", 1}}),
            "SELECT T2.x, T2.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1, T0.A3 AS A2 FROM F AS T0) AS T2, (SELECT T1.A1 "
            "AS x FROM G AS T1) AS T3 WHERE T2.x = T3.x");
}

TEST(TranslationTest, PartialApplication) {
  EXPECT_EQ(TranslateRelExpression("F[x]"), "SELECT T0.A1 AS x, T0.A2 AS A1 FROM F AS T0");
  // TODO: Fix this test
  // EXPECT_EQ(TranslateRelExpression("F[9]", {{"F", 2}}),
  //           "SELECT T0.A2 AS A1 FROM F AS T0, (SELECT 9 AS A1) AS T1 WHERE T0.A1 = T1.A1");
}

TEST(TranslationTest, AggregateExpression) {
  EXPECT_EQ(TranslateRelExpression("sum[F]", {{"F", 2}}),
            "SELECT SUM(T1.A1) FROM (SELECT T0.A1, T0.A2 AS A1 FROM F AS T0) AS T1");
}

TEST(TranslationTest, RelationalAbstraction) {
  EXPECT_EQ(TranslateRelExpression("{(1,2); (3,4)}"),
            "SELECT CASE WHEN Ind0.I = 1 THEN T2.A1 WHEN Ind0.I = 1 THEN T5.A1 END AS A1, CASE WHEN Ind0.I = 2 THEN "
            "T2.A2 WHEN Ind0.I = 2 THEN T5.A2 END AS A2 FROM (SELECT T0.A1, T1.A1 FROM (SELECT 1 AS A1) AS T0, (SELECT "
            "2 AS A1) AS T1) AS T2, (SELECT T3.A1, T4.A1 FROM (SELECT 3 AS A1) AS T3, (SELECT 4 AS A1) AS T4) AS T5, "
            "(VALUES (1), (2)) AS Ind0(I)");
}

TEST(TranslationTest, BindingExpression) {
  EXPECT_EQ(TranslateRelExpression("[x in T, y in R]: F[x, y]", {{"T", 1}, {"R", 1}, {"F", 3}}),
            "WITH S1 AS (SELECT * FROM T) WITH S0 AS (SELECT * FROM R) SELECT T1.x, T1.y, S1.x AS A1, S0.y AS A2, "
            "T1.A1 AS A3 FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS A1, T0.A4 AS A2, T0.A5 AS A3 FROM F AS T0) AS "
            "T1, S1, S0 WHERE S1.x = T1.x AND S0.y = T1.y");
}

TEST(TranslationTest, BindingExpressionBounded) {
  EXPECT_EQ(TranslateRelExpression("[x in T, y]: F[x, y] where R(y)", {{"T", 1}, {"R", 1}, {"F", 3}}),
            "WITH S1 AS (SELECT * FROM T) WITH S0 AS (SELECT * FROM R) SELECT T4.x, T4.y, S1.x AS A1, S0.y AS A2, "
            "T4.A1 AS A3 FROM (SELECT T2.x, T2.y, T2.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS A1, T0.A4 AS A2, "
            "T0.A5 AS A3 FROM F AS T0) AS T2, (SELECT T1.A1 AS y FROM R AS T1) AS T3 WHERE T2.y = T3.y) AS T4, S1, S0 "
            "WHERE S1.x = T4.x AND S0.y = T4.y");
}

TEST(TranslationTest, BindingFormula) {
  EXPECT_EQ(TranslateRelExpression("[x in T, y in R]: F(x, y)"),
            "WITH S1 AS (SELECT * FROM T) WITH S0 AS (SELECT * FROM R) SELECT T1.x, T1.y, S1.x AS A1, S0.y AS A2 FROM "
            "(SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1, S1, S0 WHERE S1.x = T1.x AND S0.y = T1.y");
}

TEST(TranslationTest, Program) {
  EXPECT_EQ(TranslateRelProgram("def F {[x in H]: G[x]}", {{"H", 1}, {"G", 2}}),
            "CREATE VIEW F AS (WITH S0 AS (SELECT * FROM H) SELECT T1.x, S0.x AS A1, T1.A1 AS A2 FROM (SELECT T0.A1 AS "
            "x, T0.A2 AS A1, T0.A3 AS A2 FROM G AS T0) AS T1, S0 WHERE S0.x = T1.x)");
}
