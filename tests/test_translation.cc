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

std::string TranslateRelFormula(const std::string& input,
                                std::unordered_map<std::string, int> external_arity_map = {}) {
  /*
   * This function takes a string CoreRel formula input and returns the SQL translation.
   */
  auto parser = rel_parser::GetParser(input);
  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::FormulaContext*>(parser->formula());
  auto ast_data = std::make_shared<ExtendedASTData>(external_arity_map);
  auto ast = rel_parser::GetExtendedASTFromTree(tree, ast_data);
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

TEST(TranslationTest, RepeatedVariableFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x, x)"), "SELECT T0.A1 AS x FROM F AS T0 WHERE T0.A1 = T0.A2");
  EXPECT_EQ(TranslateRelFormula("F(x, x, x)"), "SELECT T0.A1 AS x FROM F AS T0 WHERE T0.A1 = T0.A2 AND T0.A2 = T0.A3");
  EXPECT_EQ(TranslateRelFormula("F(x, y, x)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0 WHERE T0.A1 = T0.A3");
}

TEST(TranslationTest, OperatorFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x) and x*x > 5"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM F AS T0) AS T1 WHERE T1.x * T1.x > 5");
}

TEST(TranslationTest, ConjunctionFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x) and G(x)"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM F AS T0) AS T1, (SELECT T2.A1 AS x FROM G AS T2) AS T3 WHERE "
            "T1.x = T3.x");
}

TEST(TranslationTest, DisjunctionFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x) or G(x)"),
            "SELECT T2.x FROM (SELECT T0.A1 AS x FROM F AS T0) AS T2 UNION SELECT T3.x FROM (SELECT T1.A1 AS x FROM G "
            "AS T1) AS T3");
}

TEST(TranslationTest, ExistentialFormula) {
  EXPECT_EQ(TranslateRelFormula("exists (y | F(x, y))", {{"F", 2}}),
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

TEST(TranslationTest, ProductExpression) { EXPECT_EQ(TranslateRelExpression("(1, 2)"), "SELECT 1, 2"); }

TEST(TranslationTest, ConditionExpression) {
  EXPECT_EQ(TranslateRelExpression("F[x] where G(x)", {{"F", 2}, {"G", 1}}),
            "SELECT T2.x, T2.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM F AS T0) AS T2, (SELECT T1.A1 AS x FROM G AS "
            "T1) AS T3 WHERE T2.x = T3.x");
}

TEST(TranslationTest, PartialApplication) {
  EXPECT_EQ(TranslateRelExpression("F[x]", {{"F", 2}}), "SELECT T0.A1 AS x, T0.A2 AS A1 FROM F AS T0");
  EXPECT_EQ(TranslateRelExpression("F[1]", {{"F", 2}}), "SELECT T0.A2 AS A1 FROM F AS T0 WHERE T0.A1 = 1");
}

TEST(TranslationTest, NestedPartialApplication) {
  EXPECT_EQ(
      TranslateRelExpression("F[G[x]]", {{"F", 2}, {"G", 2}}),
      "SELECT T2.x, T0.A2 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2 WHERE T0.A1 = T2.A1");
  EXPECT_EQ(TranslateRelExpression("F[G[H[x]]]", {{"F", 2}, {"G", 2}, {"H", 2}}),
            "SELECT T4.x, T0.A2 AS A1 FROM F AS T0, (SELECT T3.x, T1.A2 AS A1 FROM G AS T1, (SELECT T2.A1 AS x, T2.A2 "
            "AS A1 FROM H AS T2) AS T3 WHERE T1.A1 = T3.A1) AS T4 WHERE T0.A1 = T4.A1");
}

TEST(TranslationTest, PartialApplicationMixedParams) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], y]", {{"F", 3}, {"G", 2}}),
            "SELECT T2.x, T0.A2 AS y, T0.A3 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2 "
            "WHERE T0.A1 = T2.A1");
  EXPECT_EQ(TranslateRelExpression("F[x, 1]", {{"F", 3}}),
            "SELECT T0.A1 AS x, T0.A3 AS A1 FROM F AS T0 WHERE T0.A2 = 1");
  EXPECT_EQ(TranslateRelExpression("F[G[x], H[y]]", {{"F", 3}, {"G", 2}, {"H", 2}}),
            "SELECT T2.x, T4.y, T0.A3 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2, (SELECT "
            "T3.A1 AS y, T3.A2 AS A1 FROM H AS T3) AS T4 WHERE T0.A1 = T2.A1 AND T0.A2 = T4.A1");
}

TEST(TranslationTest, PartialApplicationSharingVariables) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], H[x]]", {{"F", 3}, {"G", 2}, {"H", 2}}),
            "SELECT T2.x, T0.A3 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2, (SELECT T3.A1 "
            "AS x, T3.A2 AS A1 FROM H AS T3) AS T4 WHERE T0.A1 = T2.A1 AND T0.A2 = T4.A1 AND T2.x = T4.x");
  EXPECT_EQ(TranslateRelExpression("F[G[x, y], H[y, z]]", {{"F", 3}, {"G", 3}, {"H", 3}}),
            "SELECT T2.x, T2.y, T4.z, T0.A3 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS y, T1.A3 AS A1 FROM G AS "
            "T1) AS T2, (SELECT T3.A1 AS y, T3.A2 AS z, T3.A3 AS A1 FROM H AS T3) AS T4 WHERE T0.A1 = T2.A1 AND T0.A2 "
            "= T4.A1 AND T2.y = T4.y");

  EXPECT_EQ(TranslateRelExpression("F[G[x], x]", {{"F", 3}, {"G", 2}}),
            "SELECT T2.x, T0.A3 AS A1 FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2 WHERE T2.x = "
            "T0.A2 AND T0.A1 = T2.A1");

  EXPECT_EQ(TranslateRelExpression("F[G[x], x, y]", {{"F", 3}, {"G", 2}}),
            "SELECT T2.x, T0.A3 AS y FROM F AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM G AS T1) AS T2 WHERE T2.x = "
            "T0.A2 AND T0.A1 = T2.A1");
}

TEST(TranslationTest, AggregateExpression1) {
  EXPECT_EQ(TranslateRelExpression("sum[F]", {{"F", 2}}), "SELECT SUM(T0.A2) AS A1 FROM F AS T0");
  EXPECT_EQ(TranslateRelExpression("average[F]", {{"F", 2}}), "SELECT AVG(T0.A2) AS A1 FROM F AS T0");
  EXPECT_EQ(TranslateRelExpression("min[F]", {{"F", 2}}), "SELECT MIN(T0.A2) AS A1 FROM F AS T0");
  EXPECT_EQ(TranslateRelExpression("max[F]", {{"F", 2}}), "SELECT MAX(T0.A2) AS A1 FROM F AS T0");
}

TEST(TranslationTest, AggregateExpression2) {
  EXPECT_EQ(TranslateRelExpression("max[F[x]]", {{"F", 2}}),
            "SELECT T1.x, MAX(T1.A1) AS A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM F AS T0) AS T1 GROUP BY T1.x");
}

TEST(TranslationTest, RelationalAbstraction) {
  EXPECT_EQ(TranslateRelExpression("{(1,2); (3,4)}"),
            "SELECT CASE WHEN Ind0.I = 1 THEN T0.A1 WHEN Ind0.I = 2 THEN T1.A1 END AS A1, CASE WHEN Ind0.I = 1 THEN "
            "T0.A2 WHEN Ind0.I = 2 THEN T1.A2 END AS A2 FROM (SELECT 1, 2) AS T0, (SELECT 3, 4) AS T1, (VALUES (1), "
            "(2)) AS Ind0(I)");
}

TEST(TranslationTest, BindingExpression) {
  EXPECT_EQ(TranslateRelExpression("[x in T, y in R]: F[x, y]", {{"T", 1}, {"R", 1}, {"F", 3}}),
            "SELECT T.x AS A1, R.y AS A2, T1.A1 AS A3 FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS A1 FROM F AS T0) "
            "AS T1, T, R WHERE T.x = T1.x AND R.y = T1.y");
}

TEST(TranslationTest, BindingExpressionBounded) {
  EXPECT_EQ(TranslateRelExpression("[x in T, y]: F[x, y] where R(y)", {{"T", 1}, {"R", 1}, {"F", 3}}),
            "SELECT T.x AS A1, F.y AS A2, T4.A1 AS A3 FROM (SELECT T2.x, T2.y, T2.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS "
            "y, T0.A3 AS A1 FROM F AS T0) AS T2, (SELECT T1.A1 AS y FROM R AS T1) AS T3 WHERE T2.y = T3.y) AS T4, T, "
            "F, R WHERE T.x = T4.x AND F.x = T4.x AND F.y = T4.y AND R.y = T4.y AND F.y = R.y AND T.x = F.x");
}

TEST(TranslationTest, BindingFormula) {
  EXPECT_EQ(TranslateRelExpression("[x in T, y in R]: F(x, y)", {{"T", 1}, {"R", 1}, {"F", 2}}),
            "SELECT T.x AS A1, R.y AS A2 FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1, T, R WHERE T.x = "
            "T1.x AND R.y = T1.y");
}

TEST(TranslationTest, Program) {
  EXPECT_EQ(TranslateRelProgram("def F {[x in H]: G[x]}", {{"H", 1}, {"G", 2}}),
            "CREATE VIEW F AS (SELECT H.x AS A1, T1.A1 AS A2 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM G AS T0) AS T1, "
            "H WHERE H.x = T1.x)");
}

TEST(TranslationTest, TableDefinition) {
  EXPECT_EQ(TranslateRelProgram("def F {(1, 2); (3, 4)}"),
            "CREATE TABLE F AS SELECT 1 AS A1, 2 AS A2 UNION ALL SELECT 3, 4");
}
