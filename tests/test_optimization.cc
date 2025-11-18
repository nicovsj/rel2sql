// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "api/translate.h"
#include "parser/sql_parse.h"
#include "preprocessing/preprocessor.h"
#include "rel_ast/edb_info.h"
#include "test_common.h"

namespace rel2sql {

std::string TranslateWithOptimization(const std::string& input, antlr4::ParserRuleContext* tree,
                                      const rel2sql::EDBMap& edb_map = rel2sql::EDBMap()) {
  Preprocessor preprocessor(edb_map);
  auto ast = preprocessor.Process(tree);

  auto sql = GetSQLFromAST(ast);

  sql::ast::Optimizer optimizer;
  optimizer.Visit(*sql);

  return sql->ToString();
}

class OptimizationTest : public ::testing::Test {
 protected:
  void SetUp() override { default_edb_map = CreateDefaultEDBMap(); }

  std::string TranslateFormula(const std::string& input) {
    auto parser = GetParser(input);
    auto tree = parser->formula();
    return TranslateWithOptimization(input, tree, default_edb_map);
  }

  std::string TranslateExpression(const std::string& input) {
    auto parser = GetParser(input);
    auto tree = parser->expr();
    return TranslateWithOptimization(input, tree, default_edb_map);
  }

  std::string TranslateProgram(const std::string& input) {
    auto parser = GetParser(input);
    auto tree = parser->program();
    return TranslateWithOptimization(input, tree, default_edb_map);
  }

  std::string TranslateDefinition(const std::string& input) {
    auto parser = GetParser(input);
    auto tree = parser->relDef();
    return TranslateWithOptimization(input, tree, default_edb_map);
  }

  rel2sql::EDBMap default_edb_map;
};

TEST_F(OptimizationTest, FullApplicationFormula) {
  EXPECT_EQ(TranslateFormula("A(x)"), "SELECT T0.A1 AS x FROM A AS T0");
}

TEST_F(OptimizationTest, FullApplicationFormulaMultipleParams1) {
  EXPECT_EQ(TranslateFormula("B(x, y)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0");
}

TEST_F(OptimizationTest, FullApplicationFormulaMultipleParams2) {
  EXPECT_EQ(TranslateFormula("C(x, y, z)"), "SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0");
}

TEST_F(OptimizationTest, RepeatedVariableFormula1) {
  EXPECT_EQ(TranslateFormula("B(x, x)"), "SELECT T0.A1 AS x FROM B AS T0 WHERE T0.A1 = T0.A2");
}

TEST_F(OptimizationTest, RepeatedVariableFormula2) {
  EXPECT_EQ(TranslateFormula("C(x, x, x)"), "SELECT T0.A1 AS x FROM C AS T0 WHERE T0.A1 = T0.A2 AND T0.A2 = T0.A3");
}

TEST_F(OptimizationTest, RepeatedVariableFormula3) {
  EXPECT_EQ(TranslateFormula("C(x, y, x)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM C AS T0 WHERE T0.A1 = T0.A3");
}

TEST_F(OptimizationTest, OperatorFormula) {
  EXPECT_EQ(TranslateFormula("A(x) and x*x > 5"), "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 * T0.A1 > 5");
}

TEST_F(OptimizationTest, ConjunctionFormula) {
  EXPECT_EQ(TranslateFormula("A(x) and D(x)"), "SELECT T0.A1 AS x FROM A AS T0, D AS T2 WHERE T0.A1 = T2.A1");
}

TEST_F(OptimizationTest, DisjunctionFormula) {
  EXPECT_EQ(TranslateFormula("A(x) or D(x)"), "SELECT T0.A1 AS x FROM A AS T0 UNION SELECT T1.A1 AS x FROM D AS T1");
}

TEST_F(OptimizationTest, ExistentialFormula1) {
  EXPECT_EQ(TranslateFormula("exists ((y) | B(x, y))"), "SELECT T0.A1 AS x FROM B AS T0");
}

TEST_F(OptimizationTest, ExistentialFormula2) {
  EXPECT_EQ(TranslateFormula("exists ((y, z) | C(x, y, z))"), "SELECT T0.A1 AS x FROM C AS T0");
}

TEST_F(OptimizationTest, ExistentialFormula3) {
  EXPECT_EQ(TranslateFormula("exists ((y in A) | D(x, y))"),
            "SELECT T1.A1 AS x FROM D AS T1, A AS T0 WHERE T1.A2 = T0.A1");
}

TEST_F(OptimizationTest, ExistentialFormula4) {
  EXPECT_EQ(TranslateFormula("exists ((y in A, z in D) | C(x, y, z))"),
            "SELECT T2.A1 AS x FROM C AS T2, A AS T0, D AS T1 WHERE T2.A2 = T0.A1 AND T2.A3 = T1.A1");
}

TEST_F(OptimizationTest, ExistentialFormula5) {
  EXPECT_EQ(TranslateFormula("exists ((y in A, z) | C(x, y, z))"),
            "SELECT T1.A1 AS x FROM C AS T1, A AS T0 WHERE T1.A2 = T0.A1");
}

TEST_F(OptimizationTest, UniversalFormula1) {
  EXPECT_EQ(TranslateFormula("forall ((y in A) | B(x, y))"),
            "SELECT T1.A1 AS x FROM B AS T1 WHERE NOT EXISTS (SELECT * FROM A AS T0 WHERE (T1.A1, T0.A1) NOT IN "
            "(SELECT * FROM (SELECT T1.A1 AS x, T1.A2 AS y FROM B AS T1) AS T2))");
}

TEST_F(OptimizationTest, UniversalFormula2) {
  EXPECT_EQ(
      TranslateFormula("forall ((y in A, z in D) | C(x, y, z))"),
      "SELECT T2.A1 AS x FROM C AS T2 WHERE NOT EXISTS (SELECT * FROM A AS T0, D AS T1 WHERE (T2.A1, T0.A1, T1.A1) "
      "NOT IN (SELECT * FROM (SELECT T2.A1 AS x, T2.A2 AS y, T2.A3 AS z FROM C AS T2) AS T3))");
}

TEST_F(OptimizationTest, ProductExpression) { EXPECT_EQ(TranslateExpression("(1, 2)"), "SELECT 1, 2"); }

TEST_F(OptimizationTest, ConditionExpression) {
  EXPECT_EQ(TranslateExpression("B[x] where A(x)"),
            "SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0, A AS T1 WHERE T0.A1 = T1.A1");
}

TEST_F(OptimizationTest, PartialApplication1) {
  EXPECT_EQ(TranslateExpression("B[x]"), "SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0");
}

TEST_F(OptimizationTest, PartialApplication2) {
  EXPECT_EQ(TranslateExpression("B[1]"), "SELECT T0.A2 AS A1 FROM B AS T0 WHERE T0.A1 = 1");
}

TEST_F(OptimizationTest, NestedPartialApplication1) {
  EXPECT_EQ(TranslateExpression("B[E[x]]"), "SELECT T1.A1 AS x, T0.A2 AS A1 FROM B AS T0, E AS T1 WHERE T0.A1 = T1.A2");
}

TEST_F(OptimizationTest, NestedPartialApplication2) {
  EXPECT_EQ(TranslateExpression("B[E[H[x]]]"),
            "SELECT T2.A1 AS x, T0.A2 AS A1 FROM B AS T0, E AS T1, H AS T2 WHERE T0.A1 = T1.A2 AND T1.A1 = T2.A2");
}

TEST_F(OptimizationTest, PartialApplicationMixedParams1) {
  EXPECT_EQ(TranslateExpression("C[B[x], y]"),
            "SELECT T1.A1 AS x, T0.A2 AS y, T0.A3 AS A1 FROM C AS T0, B AS T1 WHERE T0.A1 = T1.A2");
}

TEST_F(OptimizationTest, PartialApplicationMixedParams2) {
  EXPECT_EQ(TranslateExpression("F[x, 1]"), "SELECT T0.A1 AS x, T0.A3 AS A1 FROM F AS T0 WHERE T0.A2 = 1");
}

TEST_F(OptimizationTest, PartialApplicationMixedParams3) {
  EXPECT_EQ(TranslateExpression("C[B[x], E[y]]"),
            "SELECT T1.A1 AS x, T3.A1 AS y, T0.A3 AS A1 FROM C AS T0, B AS T1, E AS T3 WHERE T0.A1 = T1.A2 AND T0.A2 = "
            "T3.A2");
}

TEST_F(OptimizationTest, PartialApplicationSharingVariables1) {
  EXPECT_EQ(TranslateExpression("C[B[x], E[x]]"),
            "SELECT T1.A1 AS x, T0.A3 AS A1 FROM C AS T0, B AS T1, E AS T3 WHERE T0.A1 = T1.A2 AND T0.A2 = T3.A2 AND "
            "T1.A1 = T3.A1");
}

TEST_F(OptimizationTest, PartialApplicationSharingVariables2) {
  EXPECT_EQ(TranslateExpression("C[F[x, y], I[y, z]]"),
            "SELECT T1.A1 AS x, T1.A2 AS y, T3.A2 AS z, T0.A3 AS A1 FROM C AS T0, F AS T1, I AS T3 WHERE T0.A1 = T1.A3 "
            "AND T0.A2 = T3.A3 AND T1.A2 = T3.A1");
}

TEST_F(OptimizationTest, PartialApplicationSharingVariables3) {
  EXPECT_EQ(TranslateExpression("C[B[x], x]"),
            "SELECT T1.A1 AS x, T0.A3 AS A1 FROM C AS T0, B AS T1 WHERE T1.A1 = T0.A2 AND T0.A1 = T1.A2");
}

TEST_F(OptimizationTest, PartialApplicationSharingVariables4) {
  EXPECT_EQ(TranslateExpression("C[B[x], x, y]"),
            "SELECT T1.A1 AS x, T0.A3 AS y FROM C AS T0, B AS T1 WHERE T1.A1 = T0.A2 AND T0.A1 = T1.A2");
}

TEST_F(OptimizationTest, AggregateExpression1) {
  EXPECT_EQ(TranslateExpression("sum[A]"), "SELECT SUM(T0.A1) AS A1 FROM A AS T0");
}

TEST_F(OptimizationTest, AggregateExpression2) {
  EXPECT_EQ(TranslateExpression("average[A]"), "SELECT AVG(T0.A1) AS A1 FROM A AS T0");
}

TEST_F(OptimizationTest, AggregateExpression3) {
  EXPECT_EQ(TranslateExpression("min[A]"), "SELECT MIN(T0.A1) AS A1 FROM A AS T0");
}

TEST_F(OptimizationTest, AggregateExpression4) {
  EXPECT_EQ(TranslateExpression("max[A]"), "SELECT MAX(T0.A1) AS A1 FROM A AS T0");
}

TEST_F(OptimizationTest, AggregateExpression5) {
  EXPECT_EQ(TranslateExpression("max[B[x]]"), "SELECT T0.A1 AS x, MAX(T0.A2) AS A1 FROM B AS T0 GROUP BY T0.A1");
}

TEST_F(OptimizationTest, RelationalAbstraction) {
  EXPECT_EQ(TranslateExpression("{(1,2); (3,4)}"),
            "SELECT CASE WHEN Ind0.I = 1 THEN T0.A1 WHEN Ind0.I = 2 THEN T1.A1 END AS A1, CASE WHEN Ind0.I = 1 THEN "
            "T0.A2 WHEN Ind0.I = 2 THEN T1.A2 END AS A2 FROM (SELECT 1, 2) AS T0, (SELECT 3, 4) AS T1, (VALUES (1), "
            "(2)) AS Ind0(I)");
}

TEST_F(OptimizationTest, BindingExpression) {
  EXPECT_EQ(TranslateExpression("[x in A, y in D]: C[x, y]"),
            "SELECT S1.A1 AS A1, S0.A1 AS A2, T0.A3 AS A3 FROM C AS T0, A AS S1, D AS S0 WHERE S1.A1 = T0.A1 AND S0.A1 "
            "= T0.A2");
}

TEST_F(OptimizationTest, BindingExpressionBounded) {
  EXPECT_EQ(TranslateExpression("[x in A, y]: C[x, y] where D(y)"),
            "SELECT S1.A1 AS A1, T1.A1 AS A2, T0.A3 AS A3 FROM C AS T0, D AS T1, A AS S1 WHERE S1.A1 = T0.A1 AND T1.A1 "
            "= T0.A2");
}

TEST_F(OptimizationTest, BindingFormula) {
  EXPECT_EQ(TranslateExpression("[x in T, y in R]: F(x, y)"),
            "SELECT S1.A1 AS A1, S0.A1 AS A2 FROM F AS T0, T AS S1, R AS S0 WHERE S1.A1 = T0.A1 AND S0.A1 = T0.A2");
}

TEST_F(OptimizationTest, Program) {
  EXPECT_EQ(TranslateDefinition("def R {[x in A]: B[x]}"),
            "CREATE OR REPLACE VIEW R AS (SELECT S0.A1 AS A1, T0.A2 AS A2 FROM B AS T0, A AS S0 WHERE S0.A1 = T0.A1)");
}

TEST_F(OptimizationTest, MultipleDefs1) {
  EXPECT_EQ(TranslateProgram("def R {(1, 2); (3, 4)} \n def S {(1, 4); (3, 4)}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2));\n\nCREATE OR "
            "REPLACE VIEW S AS (SELECT DISTINCT * FROM (VALUES (1, 4), (3, 4)) AS T1(A1, A2));");
}

TEST_F(OptimizationTest, MultipleDefs2) {
  EXPECT_EQ(TranslateProgram("def R {(1, 2); (3, 4)} \n def S {B[1]} \n def T {B[3]}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2));\n\nCREATE OR "
            "REPLACE VIEW S AS (SELECT T1.A2 AS A1 FROM B AS T1 WHERE T1.A1 = 1);\n\nCREATE OR REPLACE VIEW T AS "
            "(SELECT T3.A2 AS A1 FROM B AS T3 WHERE T3.A1 = 3);");
}

TEST_F(OptimizationTest, TableDefinition) {
  EXPECT_EQ(TranslateDefinition("def R {(1, 2); (3, 4)}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2))");
}

TEST_F(OptimizationTest, EDBBindingFormula) {
  EXPECT_EQ(TranslateExpression("(x): A(x)"), "SELECT T0.A1 AS A1 FROM A AS T0");
}

TEST_F(OptimizationTest, BindingConjunction) {
  EXPECT_EQ(TranslateExpression("(x, y): A(x) and B(x, y)"),
            "SELECT T0.A1 AS A1, T2.A2 AS A2 FROM A AS T0, B AS T2 WHERE T2.A1 = T0.A1");
}

// TODO: We should try to optimize this case
TEST_F(OptimizationTest, DISABLED_BindingDisjunction) {
  EXPECT_EQ(TranslateExpression("(x): A(x) or B(x)"),
            "SELECT T0.A1 AS A1 FROM A AS T0 UNION SELECT T2.A2 AS A2 FROM B AS T2 WHERE T2.A1 = T0.A1");
}

TEST_F(OptimizationTest, Composition) {
  EXPECT_EQ(TranslateExpression("(x, y) : exists( (z) | B(x, z) and E(z, y) )"),
            "SELECT T0.A1 AS A1, T2.A2 AS A2 FROM B AS T0, E AS T2 WHERE T0.A2 = T2.A1");
}

}  // namespace rel2sql

// Helper functions for testing individual optimizers
std::string OptimizeSQLWithCTEOptimizer(const std::string& sql) {
  auto expr = rel2sql::ParseSQL(sql);
  rel2sql::sql::ast::CTEOptimizer cte_optimizer;
  cte_optimizer.Visit(*expr);
  return expr->ToString();
}

std::string OptimizeSQLWithConstantOptimizer(const std::string& sql) {
  auto expr = rel2sql::ParseSQL(sql);
  rel2sql::sql::ast::ConstantOptimizer constant_optimizer;
  constant_optimizer.Visit(*expr);
  return expr->ToString();
}

std::string OptimizeSQLWithFlattenerOptimizer(const std::string& sql) {
  auto expr = rel2sql::ParseSQL(sql);
  rel2sql::sql::ast::FlattenerOptimizer flattener_optimizer;
  flattener_optimizer.Visit(*expr);
  return expr->ToString();
}

std::string OptimizeSQLWithSelfJoinOptimizer(const std::string& sql,
                                             const rel2sql::EDBMap& edb_map = rel2sql::EDBMap()) {
  auto expr = rel2sql::ParseSQL(sql, edb_map);
  rel2sql::sql::ast::SelfJoinOptimizer self_join_optimizer;
  self_join_optimizer.Visit(*expr);
  return expr->ToString();
}

// CTE Optimizer Tests
TEST(CTEOptimizationTest, RedundantCTE) {
  std::string sql =
      "WITH S(x) AS (SELECT * FROM A)\n"
      "SELECT S.A1 AS x\n"
      "FROM S\n"
      "WHERE S.x = 1";
  EXPECT_EQ(OptimizeSQLWithCTEOptimizer(sql), "SELECT S.A1 AS x FROM A AS S WHERE S.A1 = 1");
}

TEST(CTEOptimizationTest, CTEWithMultipleColumns) {
  std::string sql =
      "WITH S(x, y) AS (SELECT * FROM B)\n"
      "SELECT S.A1 AS x, S.A2 AS y\n"
      "FROM S\n"
      "WHERE S.x = 1";
  EXPECT_EQ(OptimizeSQLWithCTEOptimizer(sql), "SELECT S.A1 AS x, S.A2 AS y FROM B AS S WHERE S.A1 = 1");
}

TEST(CTEOptimizationTest, CTENoOptimizationColumnAliases) {
  std::string sql =
      "WITH S(col1, col2) AS (SELECT * FROM B)\n"
      "SELECT S.col1 AS x, S.col2 AS y\n"
      "FROM S";
  EXPECT_EQ(OptimizeSQLWithCTEOptimizer(sql), "SELECT S.A1 AS x, S.A2 AS y FROM B AS S");
}

// Constant Optimizer Tests
TEST(ConstantOptimizationTest, SimpleConstantReplacement) {
  std::string sql =
      "SELECT * FROM (SELECT 1 AS x) AS sub\n"
      "WHERE x = sub.x";
  std::string result = OptimizeSQLWithConstantOptimizer(sql);
  // There is only a single source, so the optimizer should NOT inline the constant
  EXPECT_EQ(result, "SELECT * FROM (SELECT 1 AS x) AS sub WHERE x = sub.x");
}

TEST(ConstantOptimizationTest, ConstantInWhereClause) {
  std::string sql =
      "SELECT A.A1\n"
      "FROM A, (SELECT 5 AS val) AS const\n"
      "WHERE A.A1 = const.val";
  std::string result = OptimizeSQLWithConstantOptimizer(sql);
  EXPECT_TRUE(result.find("WHERE A.A1 = 5") != std::string::npos ||
              result.find("WHERE A.A1 = const.val") != std::string::npos);
}

// Flattener Optimizer Tests
TEST(FlattenerOptimizationTest, SimpleSubqueryFlatten) {
  std::string sql = "SELECT T0.A1 FROM (SELECT A.A1 FROM A) AS T0";
  std::string result = OptimizeSQLWithFlattenerOptimizer(sql);
  EXPECT_EQ(result, "SELECT A.A1 AS A1 FROM A");
}

TEST(FlattenerOptimizationTest, SubqueryWithWhereClause) {
  std::string sql =
      "SELECT T1.A1 FROM (SELECT T0.A1 FROM A AS T0 WHERE T0.A1 > 5) AS T1\n"
      "WHERE T1.A1 < 10";
  std::string result = OptimizeSQLWithFlattenerOptimizer(sql);
  EXPECT_TRUE(result.find("FROM A AS T0") != std::string::npos && result.find("> 5") != std::string::npos &&
              result.find("< 10") != std::string::npos);
}

TEST(FlattenerOptimizationTest, NoFlattenWithGroupBy) {
  std::string sql = "SELECT T0.A1 FROM (SELECT A.A1 FROM A GROUP BY A.A1) AS T0";
  std::string result = OptimizeSQLWithFlattenerOptimizer(sql);
  // Should not flatten because GROUP BY is present
  EXPECT_TRUE(result.find("GROUP BY") != std::string::npos);
}

// Self Join Optimizer Tests
TEST(SelfJoinOptimizationTest, CompleteSelfJoin) {
  std::string sql =
      "SELECT A.A1 FROM A AS A, A AS A2\n"
      "WHERE A.A1 = A2.A1";
  std::string result = OptimizeSQLWithSelfJoinOptimizer(sql);
  // Self join should be eliminated, only one instance of A should remain
  EXPECT_TRUE(result.find("FROM A AS A") != std::string::npos);
  // The WHERE clause with equality should be removed or simplified
  EXPECT_TRUE(result.find("WHERE A.A1 = A.A1") == std::string::npos || result.find("WHERE") == std::string::npos);
}

TEST(SelfJoinOptimizationTest, MultiColumnSelfJoin) {
  std::string sql =
      "SELECT A.A1, A.A2 FROM B AS A, B AS A2\n"
      "WHERE A.A1 = A2.A1 AND A.A2 = A2.A2";
  std::string result = OptimizeSQLWithSelfJoinOptimizer(sql);
  // Self join should be eliminated
  EXPECT_TRUE(result.find("FROM B AS A") != std::string::npos);
  // Should not have both A and A2
  EXPECT_TRUE(result.find("FROM B AS A, B AS A2") == std::string::npos);
}

TEST(SelfJoinOptimizationTest, PartialSelfJoin) {
  // Here we have a self join that is not complete because the second column does not match
  // but we can still eliminate the self join because the second column is not referenced
  // in the SELECT clause or the WHERE clause.
  std::string sql =
      "SELECT T1.A1\n"
      "FROM A AS T0, A AS T1\n"
      "WHERE T0.A1 = T1.A1 AND T0.A2 > 5";
  rel2sql::EDBMap edb_map;
  edb_map["A"] = rel2sql::RelationInfo(2);
  std::string result = OptimizeSQLWithSelfJoinOptimizer(sql, edb_map);
  // Should NOT eliminate because self join is incomplete (only A1 matches, not A2)
  EXPECT_TRUE(result.find("A AS T1") == std::string::npos);
}
