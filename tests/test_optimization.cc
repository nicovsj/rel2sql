// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "rel2sql/translate.h"
#include "structs/sql_ast.h"

std::string TranslateRelProgram(const std::string& input,
                                std::unordered_map<std::string, int> external_arity_map = {}) {
  /*
   * This function takes a string CoreRel program input and returns the SQL translation.
   */
  auto parser = rel_parser::GetParser(input);
  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::ProgramContext*>(parser->program());
  auto ast_data = std::make_shared<ExtendedASTData>(external_arity_map);
  auto ast = rel_parser::GetExtendedASTFromTree(tree, ast_data);
  auto result = rel_parser::GetSQLFromTree(tree, ast);
  sql::ast::Optimizer optimizer;
  optimizer.Visit(*result);
  std::ostringstream os;
  os << *result;
  return os.str();
}

std::string TranslateRelDef(const std::string& input, std::unordered_map<std::string, int> external_arity_map = {}) {
  /*
   * This function takes a string CoreRel program input and returns the SQL translation.
   */
  auto parser = rel_parser::GetParser(input);
  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::RelDefContext*>(parser->relDef());
  auto ast_data = std::make_shared<ExtendedASTData>(external_arity_map);
  auto ast = rel_parser::GetExtendedASTFromTree(tree, ast_data);
  auto result = rel_parser::GetSQLFromTree(tree, ast);
  sql::ast::Optimizer optimizer;
  optimizer.Visit(*result);
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
  auto result = rel_parser::GetSQLFromTree(tree, ast);
  sql::ast::Optimizer optimizer;
  optimizer.Visit(*result);
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
  sql::ast::Optimizer optimizer;
  optimizer.Visit(*result);
  std::ostringstream os;
  os << *result;
  return os.str();
}

// Lets convene that we have 3 relations:
//   def F { 1; 2; 3}
//   def G { (1, 2); (2, 3); (3, 4)}
//   def H { (1, 2, 3); (2, 3, 4); (3, 4, 5)}

TEST(OptimizationTest, FullApplicationFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x)"), "SELECT T0.A1 AS x FROM F AS T0");
}

TEST(OptimizationTest, FullApplicationFormulaMultipleParams1) {
  EXPECT_EQ(TranslateRelFormula("G(x, y)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM G AS T0");
}

TEST(OptimizationTest, FullApplicationFormulaMultipleParams2) {
  EXPECT_EQ(TranslateRelFormula("H(x, y, z)"), "SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM H AS T0");
}

TEST(OptimizationTest, RepeatedVariableFormula1) {
  EXPECT_EQ(TranslateRelFormula("G(x, x)"), "SELECT T0.A1 AS x FROM G AS T0 WHERE T0.A1 = T0.A2");
}

TEST(OptimizationTest, RepeatedVariableFormula2) {
  EXPECT_EQ(TranslateRelFormula("H(x, x, x)"), "SELECT T0.A1 AS x FROM H AS T0 WHERE T0.A1 = T0.A2 AND T0.A2 = T0.A3");
}

TEST(OptimizationTest, RepeatedVariableFormula3) {
  EXPECT_EQ(TranslateRelFormula("H(x, y, x)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM H AS T0 WHERE T0.A1 = T0.A3");
}

TEST(OptimizationTest, OperatorFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x) and x*x > 5"), "SELECT T0.A1 AS x FROM F AS T0 WHERE T0.A1 * T0.A1 > 5");
}

TEST(OptimizationTest, ConjunctionFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x) and G(x)"), "SELECT T0.A1 AS x FROM F AS T0, G AS T2 WHERE T0.A1 = T2.A1");
}

TEST(OptimizationTest, DisjunctionFormula) {
  EXPECT_EQ(TranslateRelFormula("F(x) or G(x)"), "SELECT T0.A1 AS x FROM F AS T0 UNION SELECT T1.A1 AS x FROM G AS T1");
}

TEST(OptimizationTest, ExistentialFormula1) {
  EXPECT_EQ(TranslateRelFormula("exists (y | F(x, y))", {{"F", 2}}), "SELECT T0.A1 AS x FROM F AS T0");
}

TEST(OptimizationTest, ExistentialFormula2) {
  EXPECT_EQ(TranslateRelFormula("exists (y, z | F(x, y, z))"), "SELECT T0.A1 AS x FROM F AS T0");
}

TEST(OptimizationTest, ExistentialFormula3) {
  EXPECT_EQ(TranslateRelFormula("exists (y in G | F(x, y))"), "SELECT T0.A1 AS x FROM F AS T0, G WHERE T0.A2 = G.A1");
}

TEST(OptimizationTest, ExistentialFormula4) {
  EXPECT_EQ(TranslateRelFormula("exists (y in G, z in H | F(x, y, z))"),
            "SELECT T0.A1 AS x FROM F AS T0, G, H WHERE T0.A2 = G.A1 AND T0.A3 = H.A1");
}

TEST(OptimizationTest, ExistentialFormula5) {
  EXPECT_EQ(TranslateRelFormula("exists (y in G, z | F(x, y, z))"),
            "SELECT T0.A1 AS x FROM F AS T0, G WHERE T0.A2 = G.A1");
}

TEST(OptimizationTest, UniversalFormula1) {
  // TODO: Must remove inner-most FROM subquery alias (final "AS T1")
  EXPECT_EQ(TranslateRelFormula("forall (y in G | F(x, y))"),
            "SELECT T0.A1 AS x FROM F AS T0 WHERE EXISTS (SELECT * FROM G WHERE (T0.A1, G.A1) NOT IN (SELECT * FROM "
            "(SELECT T0.A1 AS x, T0.A2 AS y FROM F AS T0) AS T1))");
}

TEST(OptimizationTest, UniversalFormula2) {
  EXPECT_EQ(TranslateRelFormula("forall (y in G, z in H | F(x, y, z))"),
            "SELECT T0.A1 AS x FROM F AS T0 WHERE EXISTS (SELECT * FROM G, H WHERE (T0.A1, G.A1, H.A1) NOT IN (SELECT "
            "* FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM F AS T0) AS T1))");
}

TEST(OptimizationTest, ProductExpression) { EXPECT_EQ(TranslateRelExpression("(1, 2)"), "SELECT 1, 2"); }

TEST(OptimizationTest, ConditionExpression) {
  EXPECT_EQ(TranslateRelExpression("F[x] where G(x)", {{"F", 2}, {"G", 1}}),
            "SELECT T0.A1 AS x, T0.A2 AS A1 FROM F AS T0, G AS T1 WHERE T0.A1 = T1.A1");
}

TEST(OptimizationTest, PartialApplication1) {
  EXPECT_EQ(TranslateRelExpression("F[x]", {{"F", 2}}), "SELECT T0.A1 AS x, T0.A2 AS A1 FROM F AS T0");
}

TEST(OptimizationTest, PartialApplication2) {
  EXPECT_EQ(TranslateRelExpression("F[1]", {{"F", 2}}), "SELECT T0.A2 AS A1 FROM F AS T0 WHERE T0.A1 = 1");
}

TEST(OptimizationTest, NestedPartialApplication1) {
  EXPECT_EQ(TranslateRelExpression("F[G[x]]", {{"F", 2}, {"G", 2}}),
            "SELECT T1.A1 AS x, T0.A2 AS A1 FROM F AS T0, G AS T1 WHERE T0.A1 = T1.A2");
}

TEST(OptimizationTest, NestedPartialApplication2) {
  EXPECT_EQ(TranslateRelExpression("F[G[H[x]]]", {{"F", 2}, {"G", 2}, {"H", 2}}),
            "SELECT T2.A1 AS x, T0.A2 AS A1 FROM F AS T0, G AS T1, H AS T2 WHERE T0.A1 = T1.A2 AND T1.A1 = T2.A2");
}

TEST(OptimizationTest, PartialApplicationMixedParams1) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], y]", {{"F", 3}, {"G", 2}}),
            "SELECT T1.A1 AS x, T0.A2 AS y, T0.A3 AS A1 FROM F AS T0, G AS T1 WHERE T0.A1 = T1.A2");
}

TEST(OptimizationTest, PartialApplicationMixedParams2) {
  EXPECT_EQ(TranslateRelExpression("F[x, 1]", {{"F", 3}}),
            "SELECT T0.A1 AS x, T0.A3 AS A1 FROM F AS T0 WHERE T0.A2 = 1");
}

TEST(OptimizationTest, PartialApplicationMixedParams3) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], H[y]]", {{"F", 3}, {"G", 2}, {"H", 2}}),
            "SELECT T1.A1 AS x, T3.A1 AS y, T0.A3 AS A1 FROM F AS T0, G AS T1, H AS T3 WHERE T0.A1 = T1.A2 AND T0.A2 = "
            "T3.A2");
}

TEST(OptimizationTest, PartialApplicationSharingVariables1) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], H[x]]", {{"F", 3}, {"G", 2}, {"H", 2}}),
            "SELECT T1.A1 AS x, T0.A3 AS A1 FROM F AS T0, G AS T1, H AS T3 WHERE T0.A1 = T1.A2 AND T0.A2 = T3.A2 AND "
            "T1.A1 = T3.A1");
}

TEST(OptimizationTest, PartialApplicationSharingVariables2) {
  EXPECT_EQ(TranslateRelExpression("F[G[x, y], H[y, z]]", {{"F", 3}, {"G", 3}, {"H", 3}}),
            "SELECT T1.A1 AS x, T1.A2 AS y, T3.A2 AS z, T0.A3 AS A1 FROM F AS T0, G AS T1, H AS T3 WHERE T0.A1 = T1.A3 "
            "AND T0.A2 = T3.A3 AND T1.A2 = T3.A1");
}

TEST(OptimizationTest, PartialApplicationSharingVariables3) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], x]", {{"F", 3}, {"G", 2}}),
            "SELECT T1.A1 AS x, T0.A3 AS A1 FROM F AS T0, G AS T1 WHERE T1.A1 = T0.A2 AND T0.A1 = T1.A2");
}

TEST(OptimizationTest, PartialApplicationSharingVariables4) {
  EXPECT_EQ(TranslateRelExpression("F[G[x], x, y]", {{"F", 3}, {"G", 2}}),
            "SELECT T1.A1 AS x, T0.A3 AS y FROM F AS T0, G AS T1 WHERE T1.A1 = T0.A2 AND T0.A1 = T1.A2");
}

TEST(OptimizationTest, AggregateExpression1) {
  EXPECT_EQ(TranslateRelExpression("sum[F]", {{"F", 2}}), "SELECT SUM(T0.A2) AS A1 FROM F AS T0");
}

TEST(OptimizationTest, AggregateExpression2) {
  EXPECT_EQ(TranslateRelExpression("average[F]", {{"F", 2}}), "SELECT AVG(T0.A2) AS A1 FROM F AS T0");
}

TEST(OptimizationTest, AggregateExpression3) {
  EXPECT_EQ(TranslateRelExpression("min[F]", {{"F", 2}}), "SELECT MIN(T0.A2) AS A1 FROM F AS T0");
}

TEST(OptimizationTest, AggregateExpression4) {
  EXPECT_EQ(TranslateRelExpression("max[F]", {{"F", 2}}), "SELECT MAX(T0.A2) AS A1 FROM F AS T0");
}

TEST(OptimizationTest, AggregateExpression5) {
  EXPECT_EQ(TranslateRelExpression("max[F[x]]", {{"F", 2}}),
            "SELECT T0.A1 AS x, MAX(T0.A2) AS A1 FROM F AS T0 GROUP BY T0.A1");
}

TEST(OptimizationTest, RelationalAbstraction) {
  EXPECT_EQ(TranslateRelExpression("{(1,2); (3,4)}"),
            "SELECT CASE WHEN Ind0.I = 1 THEN T0.A1 WHEN Ind0.I = 2 THEN T1.A1 END AS A1, CASE WHEN Ind0.I = 1 THEN "
            "T0.A2 WHEN Ind0.I = 2 THEN T1.A2 END AS A2 FROM (SELECT 1, 2) AS T0, (SELECT 3, 4) AS T1, (VALUES (1), "
            "(2)) AS Ind0(I)");
}

TEST(OptimizationTest, BindingExpression) {
  EXPECT_EQ(TranslateRelExpression("[x in T, y in R]: F[x, y]", {{"T", 1}, {"R", 1}, {"F", 3}}),
            "SELECT S1.A1 AS A1, S0.A1 AS A2, T0.A3 AS A3 FROM F AS T0, T AS S1, R AS S0 WHERE S1.A1 = T0.A1 AND S0.A1 "
            "= T0.A2");
}

TEST(OptimizationTest, BindingExpressionBounded) {
  EXPECT_EQ(TranslateRelExpression("[x in T, y]: F[x, y] where R(y)", {{"T", 1}, {"R", 1}, {"F", 3}}),
            "SELECT S1.A1 AS A1, S0.A1 AS A2, T0.A3 AS A3 FROM F AS T0, R AS T1, T AS S1, R AS S0 WHERE S1.A1 = T0.A1 "
            "AND S0.A1 = T0.A2 AND T0.A2 = T1.A1");
}

TEST(OptimizationTest, BindingFormula) {
  EXPECT_EQ(TranslateRelExpression("[x in T, y in R]: F(x, y)", {{"T", 1}, {"R", 1}, {"F", 2}}),
            "SELECT S1.A1 AS A1, S0.A1 AS A2 FROM F AS T0, T AS S1, R AS S0 WHERE S1.A1 = T0.A1 AND S0.A1 = T0.A2");
}

TEST(OptimizationTest, Program) {
  EXPECT_EQ(TranslateRelDef("def F {[x in H]: G[x]}", {{"H", 1}, {"G", 2}}),
            "CREATE VIEW F AS (SELECT S0.A1 AS A1, T0.A2 AS A2 FROM G AS T0, H AS S0 WHERE S0.A1 = T0.A1)");
}

TEST(OptimizationTest, MultipleDefs1) {
  EXPECT_EQ(TranslateRelProgram("def F {(1, 2); (3, 4)} \n def F {(1, 4); (3, 4)}"),
            "CREATE VIEW F AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4), (1, 4), (3, 4)) AS T0(A1, A2));");
}

TEST(OptimizationTest, MultipleDefs2) {
  EXPECT_EQ(TranslateRelProgram("def G {(1, 2); (3, 4)} \n def F {G[1]} \n def F {G[3]}"),
            "CREATE VIEW G AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2));\n\nCREATE VIEW F AS "
            "SELECT T1.A2 AS A1 FROM G AS T1 WHERE T1.A1 = 1 UNION SELECT T3.A2 AS A1 FROM G AS T3 WHERE T3.A1 = 3;");
}

TEST(OptimizationTest, TableDefinition) {
  EXPECT_EQ(TranslateRelDef("def F {(1, 2); (3, 4)}"),
            "CREATE VIEW F AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2))");
}
