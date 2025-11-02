// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "structs/edb_info.h"
#include "structs/sql_ast.h"
#include "translate.h"

namespace rel2sql {

std::string TranslateWithOptimization(const std::string& input, antlr4::ParserRuleContext* tree,
                                      const rel2sql::EDBMap& edb_map = rel2sql::EDBMap()) {
  auto ast_data = std::make_shared<ExtendedASTData>(edb_map);
  auto ast = GetExtendedASTFromParsingTree(tree, ast_data);
  auto result = GetSQLFromAST(tree, ast);
  sql::ast::Optimizer optimizer;
  optimizer.Visit(*result);
  std::ostringstream os;
  os << *result;
  return os.str();
}

class OptimizationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    default_edb_map["A"] = rel2sql::EDBInfo(1);
    default_edb_map["B"] = rel2sql::EDBInfo(2);
    default_edb_map["C"] = rel2sql::EDBInfo(3);

    default_edb_map["D"] = rel2sql::EDBInfo(1);
    default_edb_map["E"] = rel2sql::EDBInfo(2);
    default_edb_map["F"] = rel2sql::EDBInfo(3);

    default_edb_map["G"] = rel2sql::EDBInfo(1);
    default_edb_map["H"] = rel2sql::EDBInfo(2);
    default_edb_map["I"] = rel2sql::EDBInfo(3);
  }

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
            "SELECT T1.A1 AS x FROM B AS T1 WHERE EXISTS (SELECT * FROM A AS T0 WHERE (T1.A1, T0.A1) NOT IN (SELECT * "
            "FROM (SELECT T1.A1 AS x, T1.A2 AS y FROM B AS T1) AS T2))");
}

TEST_F(OptimizationTest, UniversalFormula2) {
  EXPECT_EQ(TranslateFormula("forall ((y in A, z in D) | C(x, y, z))"),
            "SELECT T2.A1 AS x FROM C AS T2 WHERE EXISTS (SELECT * FROM A AS T0, D AS T1 WHERE (T2.A1, T0.A1, T1.A1) "
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

TEST_F(OptimizationTest, EDBBindingFormulaWithCondition) {
  EXPECT_EQ(TranslateExpression("(x, y): A(x) and B(x, y)"),
            "SELECT T0.A1 AS A1, T2.A2 AS A2 FROM A AS T0, B AS T2 WHERE T2.A1 = T0.A1");
}

}  // namespace rel2sql
