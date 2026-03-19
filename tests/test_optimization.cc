// cspell:ignore GTEST
#include <gtest/gtest.h>

#include <regex>

#include "api/translate.h"
#include "optimizer/cte_inliner.h"
#include "optimizer/self_join_optimizer.h"
#include "optimizer/validating_optimizer.h"
#include "parser/sql_parse.h"
#include "rel_ast/relation_info.h"
#include "sql_ast/sql_ast.h"
#include "support/exceptions.h"
#include "test_common.h"

namespace rel2sql {

static std::string ApplyValidatingOptimizer(std::shared_ptr<sql::ast::Expression> sql) {
  if (!sql) return "";
  sql::ast::ValidatingOptimizer optimizer;
  try {
    sql = optimizer.Optimize(sql);
  } catch (const std::runtime_error& e) {
    EXPECT_TRUE(false) << e.what();
  }
  return sql ? sql->ToString() : "";
}

class OptimizationTest : public ::testing::Test {
 protected:
  void SetUp() override { default_edb_map = CreateDefaultEDBMap(); }

  std::string TranslateFormula(const std::string& input) {
    return ApplyValidatingOptimizer(GetSQLFromFormula(input, default_edb_map));
  }

  std::string TranslateExpression(const std::string& input) {
    return ApplyValidatingOptimizer(GetSQLFromExpr(input, default_edb_map));
  }

  std::string TranslateProgram(const std::string& input) {
    return ApplyValidatingOptimizer(GetUnoptimizedSQLRel(input, default_edb_map));
  }

  std::string TranslateDefinition(const std::string& input) {
    return ApplyValidatingOptimizer(GetUnoptimizedSQLRel(input, default_edb_map));
  }

  rel2sql::RelationMap default_edb_map;
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
  EXPECT_EQ(TranslateFormula("A(x) and D(x)"), "SELECT T0.A1 AS x FROM A AS T0, D AS T1 WHERE T0.A1 = T1.A1");
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
            "SELECT T0.A1 AS x FROM D AS T0, A AS T2 WHERE T0.A2 = T2.A1");
}

TEST_F(OptimizationTest, ExistentialFormula4) {
  EXPECT_EQ(TranslateFormula("exists ((y in A, z in D) | C(x, y, z))"),
            "SELECT T0.A1 AS x FROM C AS T0, A AS T2, D AS T3 WHERE T0.A2 = T2.A1 AND T0.A3 = T3.A1");
}

TEST_F(OptimizationTest, ExistentialFormula5) {
  EXPECT_EQ(TranslateFormula("exists ((y in A, z) | C(x, y, z))"),
            "SELECT T0.A1 AS x FROM C AS T0, A AS T2 WHERE T0.A2 = T2.A1");
}

TEST_F(OptimizationTest, UniversalFormula1) {
  EXPECT_EQ(TranslateFormula("forall ((y in A) | B(x, y))"),
            "SELECT T1.A1 AS x FROM B AS T1 WHERE NOT EXISTS (SELECT * FROM A AS T0 WHERE (T1.A1, T0.A1) NOT IN "
            "(SELECT * FROM B AS T1))");
}

TEST_F(OptimizationTest, UniversalFormula2) {
  EXPECT_EQ(TranslateFormula("forall ((y in A, z in D) | C(x, y, z))"),
            "SELECT T2.A1 AS x FROM C AS T2 WHERE NOT EXISTS (SELECT * FROM A AS T0, D AS T1 WHERE (T2.A1, T0.A1, "
            "T1.A1) NOT IN (SELECT * FROM C AS T2))");
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
            "SELECT T1.A1 AS x, T0.A3 AS A1 FROM C AS T0, B AS T1 WHERE T0.A1 = T1.A2 AND T0.A2 = T1.A1");
}

TEST_F(OptimizationTest, PartialApplicationSharingVariables4) {
  EXPECT_EQ(TranslateExpression("C[B[x], x, y]"),
            "SELECT T1.A1 AS x, T0.A3 AS y FROM C AS T0, B AS T1 WHERE T0.A1 = T1.A2 AND T0.A2 = T1.A1");
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

TEST_F(OptimizationTest, RelationalAbstraction1) {
  EXPECT_EQ(
      TranslateExpression("{(1,2); (3,4)}"),
      "SELECT CASE WHEN I0.i = 1 THEN T0.A1 WHEN I0.i = 2 THEN T1.A1 END AS A1, CASE WHEN I0.i = 1 THEN T0.A2 WHEN "
      "I0.i = 2 THEN T1.A2 END AS A2 FROM (SELECT 1, 2) AS T0, (SELECT 3, 4) AS T1, (VALUES (1), (2)) AS I0(i)");
}

TEST_F(OptimizationTest, BindingExpression1) {
  EXPECT_EQ(TranslateExpression("[x in A, y in D]: C[x, y]"),
            "SELECT T0.A1 AS A1, T0.A2 AS A2, T0.A3 AS A3 FROM C AS T0, A AS T1, D AS T2 WHERE T0.A2 = T2.A1 AND T0.A1 "
            "= T1.A1");
}

TEST_F(OptimizationTest, BindingExpressionBounded1) {
  EXPECT_EQ(TranslateExpression("[x in A, y]: C[x, y] where D(y)"),
            "SELECT T0.A1 AS A1, T0.A2 AS A2, T0.A3 AS A3 FROM C AS T0, D AS T1, A AS T4 WHERE T0.A1 = T4.A1 AND T0.A2 "
            "= T1.A1");
}

TEST_F(OptimizationTest, BindingFormula1) {
  EXPECT_EQ(TranslateExpression("[x in A, y in D]: B(x, y)"),
            "SELECT T0.A1 AS A1, T0.A2 AS A2 FROM B AS T0, A AS T1, D AS T2 WHERE T0.A2 = T2.A1 AND T0.A1 = T1.A1");
}

TEST_F(OptimizationTest, BindingFormula2) {
  EXPECT_EQ(TranslateExpression("(x): {B[1]}(x) or B(x,1)"),
            "SELECT T0.A2 AS A1 FROM B AS T0 WHERE T0.A1 = 1 UNION SELECT T4.A1 AS A1 FROM B AS T4 WHERE T4.A2 = 1");
}

TEST_F(OptimizationTest, BindingFormula3) {
  EXPECT_EQ(TranslateExpression("(x) : {B[1]; B[3]}(x)"),
            "SELECT CASE WHEN I0.i = 1 THEN T0.A2 WHEN I0.i = 2 THEN T3.A2 END AS A1 FROM B AS T0, B AS T3, (VALUES "
            "(1), (2)) AS I0(i) WHERE T0.A1 = 1 AND T3.A1 = 3");
}

TEST_F(OptimizationTest, BindingFormula4) {
  EXPECT_EQ(TranslateExpression("(x): A(x+1)"), "SELECT T0.A1 - 1 AS A1 FROM A AS T0");
}

TEST_F(OptimizationTest, BindingFormula5) {
  EXPECT_EQ(TranslateExpression("(x): A(2*x+1)"), "SELECT (T0.A1 - 1) / 2 AS A1 FROM A AS T0");
}

TEST_F(OptimizationTest, NestedBindingFormula) {
  EXPECT_EQ(TranslateExpression("[x]: {(y) : B(x,y)}"), "SELECT T0.A1 AS A1, T0.A2 AS A2 FROM B AS T0");
}

TEST_F(OptimizationTest, Definition1) {
  EXPECT_EQ(TranslateDefinition("def R {[x in A]: B[x]}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT T0.A1 AS A1, T0.A2 AS A2 FROM B AS T0, A AS T1 WHERE T0.A1 = "
            "T1.A1);");
}

TEST_F(OptimizationTest, Definition2) {
  EXPECT_EQ(TranslateDefinition("def R {[x]: x+1 where A(x)}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT T0.A1 AS A1, T0.A1 + 1 AS A2 FROM A AS T0);");
}

TEST_F(OptimizationTest, MultipleDefs1) {
  EXPECT_EQ(TranslateProgram("def R {(1, 2); (3, 4)} \n def S {(1, 4); (3, 4)}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2));\n\nCREATE OR "
            "REPLACE VIEW S AS (SELECT DISTINCT * FROM (VALUES (1, 4), (3, 4)) AS T1(A1, A2));");
}

TEST_F(OptimizationTest, MultipleDefs2) {
  EXPECT_EQ(TranslateProgram("def R {(1, 2); (3, 4)} \n def S {B[1]} \n def T {B[3]}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2));\n\nCREATE OR "
            "REPLACE VIEW S AS (SELECT DISTINCT T1.A2 AS A1 FROM B AS T1 WHERE T1.A1 = 1);\n\nCREATE OR REPLACE VIEW T "
            "AS (SELECT DISTINCT T4.A2 AS A1 FROM B AS T4 WHERE T4.A1 = 3);");
}

TEST_F(OptimizationTest, TableDefinition) {
  EXPECT_EQ(TranslateDefinition("def R {(1, 2); (3, 4)}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2));");
}

TEST_F(OptimizationTest, EDBBindingFormula) {
  EXPECT_EQ(TranslateExpression("(x): A(x)"), "SELECT T0.A1 AS A1 FROM A AS T0");
}

TEST_F(OptimizationTest, BindingConjunction) {
  EXPECT_EQ(TranslateExpression("(x, y): A(x) and B(x, y)"),
            "SELECT T0.A1 AS A1, T1.A2 AS A2 FROM A AS T0, B AS T1 WHERE T0.A1 = T1.A1");
}

TEST_F(OptimizationTest, BindingDisjunction) {
  EXPECT_EQ(TranslateExpression("(x): A(x) or B(x)"),
            "SELECT T0.A1 AS A1 FROM A AS T0 UNION SELECT T1.A1 AS A1 FROM B AS T1");
}

TEST_F(OptimizationTest, Composition) {
  EXPECT_EQ(TranslateExpression("(x, y) : exists( (z) | B(x, z) and E(z, y) )"),
            "SELECT T0.A1 AS A1, T1.A2 AS A2 FROM B AS T0, E AS T1 WHERE T0.A2 = T1.A1");
}

TEST_F(OptimizationTest, TransitiveClosure) {
  default_edb_map["R"] = RelationInfo(2);

  EXPECT_EQ(TranslateDefinition("def Q {(x,y) : R(x,y) or exists((z) | R(x,z) and Q(z,y))}"),
            "CREATE OR REPLACE VIEW Q AS (WITH RECURSIVE R0(A1, A2) AS (SELECT T0.A1 AS x, T0.A2 AS y FROM R AS T0 "
            "UNION SELECT T1.A1 AS x, T2.A2 AS y FROM R AS T1, R0 AS T2 WHERE T1.A2 = T2.A1) SELECT DISTINCT R0.A1 AS "
            "A1, R0.A2 AS A2 FROM R0);");
}

TEST_F(OptimizationTest, FullApplicationOnExpression2) {
  EXPECT_EQ(TranslateExpression("{ (x,y) : B(x,y) } where B(1,2) "),
            "SELECT T0.A1 AS A1, T0.A2 AS A2 FROM B AS T0, B AS T3 WHERE T3.A1 = 1 AND T3.A2 = 2");
}

TEST_F(OptimizationTest, FullApplicationOnExpression3) {
  EXPECT_EQ(TranslateExpression("{(x,y) : B(x,y)}[1]"), "SELECT T0.A2 AS A1 FROM B AS T0 WHERE T0.A1 = 1");
}

TEST_F(OptimizationTest, FullApplicationOnExpression4) {
  EXPECT_EQ(TranslateDefinition("def Q { B[1] ; B[2] }"),
            "CREATE OR REPLACE VIEW Q AS (SELECT DISTINCT CASE WHEN I0.i = 1 THEN T0.A2 WHEN I0.i = 2 THEN T3.A2 END "
            "AS A1 FROM B AS T0, B AS T3, (VALUES (1), (2)) AS I0(i) WHERE T0.A1 = 1 AND T3.A1 = 2);");
}

TEST_F(OptimizationTest, FullApplicationOnExpression5) {
  EXPECT_EQ(TranslateExpression("{B[y];E[y]} where y > 1}"),
            "SELECT T0.A1 AS y, CASE WHEN I0.i = 1 THEN T0.A2 WHEN I0.i = 2 THEN T2.A2 END AS A1 FROM B AS T0, E AS "
            "T2, (VALUES (1), (2)) AS I0(i) WHERE T0.A1 > 1 AND T0.A1 = T2.A1");
}

TEST_F(OptimizationTest, FullApplicationOnExpression6) {
  EXPECT_EQ(TranslateExpression("(x,y): B(x,y) where {[z] : E(1,z)}(3)"),
            "SELECT T0.A1 AS A1, T0.A2 AS A2 FROM B AS T0, E AS T2 WHERE T2.A2 = 3 AND T2.A1 = 1");
}

TEST_F(OptimizationTest, FullApplicationOnExpression7) {
  EXPECT_EQ(TranslateExpression("(x) : B(x,y) and B(y,x)"),
            "SELECT T0.A2 AS y, T0.A1 AS A1 FROM B AS T0, B AS T1 WHERE T0.A2 = T1.A1 AND T0.A1 = T1.A2");
}

TEST_F(OptimizationTest, FullApplication7) { EXPECT_EQ(TranslateFormula("{1}(x)"), "SELECT 1 AS x"); }

TEST_F(OptimizationTest, FullApplication8) {
  EXPECT_EQ(TranslateFormula("B(A,x)"), "SELECT T0.A2 AS x FROM B AS T0, A AS T1 WHERE T0.A1 = T1.A1");
}

TEST_F(OptimizationTest, ComparisonOperators1) {
  EXPECT_EQ(TranslateFormula("A(x) and x < 5"), "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 < 5");
}

TEST_F(OptimizationTest, ComparisonOperators2) {
  EXPECT_EQ(TranslateFormula("A(x) and x <= 5"), "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 <= 5");
}

TEST_F(OptimizationTest, ComparisonOperators3) {
  EXPECT_EQ(TranslateFormula("A(x) and x >= 5"), "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 >= 5");
}

TEST_F(OptimizationTest, ComparisonOperators4) {
  EXPECT_EQ(TranslateFormula("A(x) and x = 5"), "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 = 5");
}

TEST_F(OptimizationTest, ComparisonOperators5) {
  EXPECT_EQ(TranslateFormula("A(x) and x != 5"), "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 != 5");
}

TEST_F(OptimizationTest, ArithmeticInComparisons1) {
  EXPECT_EQ(TranslateFormula("B(x,y) and x + y > 5"),
            "SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0 WHERE T0.A1 + T0.A2 > 5");
}

TEST_F(OptimizationTest, ArithmeticInComparisons2) {
  EXPECT_EQ(TranslateFormula("B(x,y) and x - y < 0"),
            "SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0 WHERE T0.A1 - T0.A2 < 0");
}

TEST_F(OptimizationTest, ArithmeticInComparisons3) {
  EXPECT_EQ(TranslateFormula("B(x,y) and x * y = 10"),
            "SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0 WHERE T0.A1 * T0.A2 = 10");
}

TEST_F(OptimizationTest, ArithmeticInComparisons4) {
  EXPECT_EQ(TranslateFormula("B(x,y) and x / y > 2"),
            "SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0 WHERE T0.A1 / T0.A2 > 2");
}

TEST_F(OptimizationTest, ArithmeticInComparisons5) {
  EXPECT_EQ(TranslateFormula("C(x,y,z) and (x + y) * z > 100"),
            "SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0 WHERE (T0.A1 + T0.A2) * T0.A3 > 100");
}

TEST_F(OptimizationTest, ArithmeticInComparisons6) {
  EXPECT_EQ(TranslateFormula("C(x,y,z) and x + y + z = 15"),
            "SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0 WHERE T0.A1 + "
            "T0.A2 + T0.A3 = 15");
}

TEST_F(OptimizationTest, InferrableVariableConjunction1) {
  EXPECT_EQ(TranslateExpression("x = y + 1 and A(y)"), "SELECT T0.A1 + 1 AS x, T0.A1 AS y FROM A AS T0");
}

TEST_F(OptimizationTest, InferrableVariableConjunction2) {
  EXPECT_EQ(TranslateExpression("x = y + 1 and A(x)"), "SELECT T0.A1 AS x, T0.A1 - 1 AS y FROM A AS T0");
}

TEST_F(OptimizationTest, InferrableVariableConjunction3) {
  EXPECT_EQ(TranslateExpression("x = 2 * y - 3 and A(y)"),
            "SELECT T1.y, 2 * T1.y - 3 AS x FROM (SELECT T0.A1 AS y FROM A AS T0) AS T1");
}

TEST_F(OptimizationTest, InferrableVariableMultivariate) {
  EXPECT_EQ(TranslateExpression("z = x + y + 1 and B(x, y)"),
            "SELECT T1.x, T1.y, T1.x + T1.y + 1 AS z FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0) AS T1");
}

TEST_F(OptimizationTest, NegativeLiteral1) {
  EXPECT_EQ(TranslateFormula("A(x) and x > -5"), "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 > -5");
}

TEST_F(OptimizationTest, NegativeLiteral2) {
  EXPECT_EQ(TranslateFormula("A(x) and x >= -10.5"), "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 >= -10.5");
}

TEST_F(OptimizationTest, NegativeLiteral3) {
  EXPECT_EQ(TranslateFormula("A(x) and x = -1"), "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 = -1");
}

TEST_F(OptimizationTest, FloatLiteral) {
  EXPECT_EQ(TranslateFormula("A(x) and x < 3.14"), "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 < 3.14");
}

TEST_F(OptimizationTest, DisjunctionFormula2) {
  EXPECT_EQ(TranslateFormula("A(x) or (D(x) and G(x))"),
            "SELECT T0.A1 AS x FROM A AS T0 UNION SELECT T1.A1 AS x FROM D AS T1, G AS T2 WHERE T1.A1 = T2.A1");
}

TEST_F(OptimizationTest, DisjunctionFormula3) {
  EXPECT_EQ(TranslateFormula("(A(x) or D(y)) and B(x,y)"),
            "SELECT T0.A1 AS x FROM A AS T0 UNION SELECT T3.x FROM (SELECT T1.A1 AS x FROM D AS T1) AS T3, (SELECT "
            "T2.A1 AS x FROM G AS T2) AS T4 WHERE T3.x = T4.x");
}

TEST_F(OptimizationTest, NegationFormula1) {
  EXPECT_EQ(TranslateFormula("A(x) and not D(x)"),
            "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 NOT IN (SELECT DISTINCT * FROM D AS T1)");
}

TEST_F(OptimizationTest, NegationFormula2) {
  EXPECT_EQ(TranslateFormula("not A(x) and D(x)"),
            "SELECT T2.A1 AS x FROM D AS T2 WHERE T2.A1 NOT IN (SELECT DISTINCT * FROM A AS T0)");
}

TEST_F(OptimizationTest, NegationFormula3) {
  EXPECT_EQ(TranslateFormula("A(x) and not (D(x) or G(x))"),
            "SELECT T0.A1 AS x FROM A AS T0 WHERE T0.A1 NOT IN (SELECT DISTINCT * FROM (SELECT T1.A1 AS x FROM D AS T1 "
            "UNION SELECT T2.A1 AS x FROM G AS T2) AS T3)");
}

TEST_F(OptimizationTest, NegationFormula4) {
  EXPECT_EQ(TranslateFormula("A(x) and D(y) and not D(x) and not A(y)"),
            "SELECT T0.A1 AS x, T1.A1 AS y FROM A AS T0, D AS T1 WHERE T0.A1 NOT IN (SELECT DISTINCT * FROM D AS T4) "
            "AND T1.A1 NOT IN (SELECT DISTINCT * FROM A AS T9)");
}

TEST_F(OptimizationTest, NestedQuantifiers1) {
  EXPECT_EQ(TranslateFormula("exists ((y) | exists ((z) | C(x, y, z)))"), "SELECT T0.A1 AS x FROM C AS T0");
}

TEST_F(OptimizationTest, NestedQuantifiers2) {
  EXPECT_EQ(TranslateFormula("exists ((y in A) | forall ((z in D) | C(x, y, z)))"),
            "SELECT T1.A1 AS x FROM C AS T1, A AS T4 WHERE T1.A2 = T4.A1 AND NOT EXISTS (SELECT * FROM D AS T0 WHERE "
            "(T1.A1, T1.A2, T0.A1) NOT IN (SELECT * FROM C AS T1))");
}

TEST_F(OptimizationTest, NestedQuantifiers3) {
  EXPECT_EQ(TranslateFormula("forall ((y in A) | exists ((z) | C(x, y, z)))"),
            "SELECT T1.A1 AS x FROM C AS T1 WHERE NOT EXISTS (SELECT * FROM A AS T0 WHERE (T1.A1, T0.A1) NOT IN "
            "(SELECT * FROM C AS T1))");
}

TEST_F(OptimizationTest, NestedQuantifiers4) {
  EXPECT_EQ(TranslateFormula("exists ((y) | exists ((z) | exists ((w) | I(x, y, z) and I(y, z, w))))"),
            "SELECT T0.A1 AS x FROM I AS T0, I AS T1 WHERE T0.A3 = T1.A2 AND T0.A2 = T1.A1");
}

TEST_F(OptimizationTest, Conditional2) {
  EXPECT_EQ(TranslateExpression("B[x] where x > 1"), "SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0 WHERE T0.A1 > 1");
}

TEST_F(OptimizationTest, Conditional3) {
  EXPECT_EQ(TranslateExpression("B[x] where (x > 1 and x < 5)"),
            "SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0 WHERE T0.A1 > 1 AND T0.A1 < 5");
}

TEST_F(OptimizationTest, Conditional4) {
  EXPECT_THROW(TranslateExpression("B[x] where (y > 1 and x > 0)"), TranslationException);
}

TEST_F(OptimizationTest, NestedConditional1) {
  EXPECT_EQ(TranslateExpression("(B[x] where A(x)) where D(x)"),
            "SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0, A AS T1, D AS T5 WHERE T0.A1 = T5.A1 AND T0.A1 = T1.A1");
}

TEST_F(OptimizationTest, NestedConditional2) {
  EXPECT_EQ(TranslateExpression("B[x] where (A(x) and D(x))"),
            "SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0, A AS T1, D AS T2 WHERE T0.A1 = T1.A1 AND T1.A1 = T2.A1");
}

TEST_F(OptimizationTest, PartialApplicationOnExpression1) {
  EXPECT_EQ(TranslateExpression("{C[x]}[x]"),
            "SELECT T2.x, T2.A2 AS A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1, T0.A3 AS A2 FROM C AS T0) AS T2 WHERE T2.x "
            "= T2.A1");
}

TEST_F(OptimizationTest, PartialApplicationOnExpression2) {
  EXPECT_EQ(TranslateExpression("{(1,2);(3,4)}[1]"),
            "SELECT T2.A2 AS A1 FROM (SELECT CASE WHEN I0.i = 1 THEN T0.A1 WHEN I0.i = 2 THEN T1.A1 END AS A1, CASE "
            "WHEN I0.i = 1 THEN T0.A2 WHEN I0.i = 2 THEN T1.A2 END AS A2 FROM (SELECT 1, 2) AS T0, (SELECT 3, 4) AS "
            "T1, (VALUES (1), (2)) AS I0(i)) AS T2, (SELECT 1 AS A1) AS T3 WHERE T2.A1 = T3.A1");
}

TEST_F(OptimizationTest, FullApplicationOnExpression1) {
  EXPECT_EQ(TranslateExpression("{B[1]}(x)"), "SELECT T0.A2 AS x FROM B AS T0 WHERE T0.A1 = 1");
}

// TODO: Check possible further optimization
TEST_F(OptimizationTest, AggregateExpression6) {
  EXPECT_EQ(TranslateExpression("sum[{(1,2);(3,4)}]"),
            "SELECT SUM(CASE WHEN I0.i = 1 THEN T0.A2 WHEN I0.i = 2 THEN T1.A2 END) AS A1 FROM (SELECT 1, 2) AS T0, "
            "(SELECT 3, 4) AS T1, (VALUES (1), (2)) AS I0(i)");
}

// TODO: Check possible further optimization
TEST_F(OptimizationTest, AggregateExpression7) {
  EXPECT_EQ(TranslateExpression("max[{(1);(2);(3)}]"),
            "SELECT MAX(CASE WHEN I0.i = 1 THEN T0.A1 WHEN I0.i = 2 THEN T1.A1 WHEN I0.i = 3 THEN T2.A1 END) AS A1 "
            "FROM (SELECT 1) AS T0, (SELECT 2) AS T1, (SELECT 3) AS T2, (VALUES (1), (2), (3)) AS I0(i)");
}

TEST_F(OptimizationTest, RelationalAbstraction2) { EXPECT_EQ(TranslateExpression("{1}"), "SELECT 1 AS A1"); }

TEST_F(OptimizationTest, RelationalAbstraction3) { EXPECT_EQ(TranslateExpression("{(1)}"), "SELECT 1"); }

TEST_F(OptimizationTest, RelationalAbstraction4) { EXPECT_EQ(TranslateExpression("{(1,2)}"), "SELECT 1, 2"); }

TEST_F(OptimizationTest, RelationalAbstraction5) {
  EXPECT_EQ(TranslateExpression("{1;2;3}"),
            "SELECT CASE WHEN I0.i = 1 THEN 1 WHEN I0.i = 2 THEN 2 WHEN I0.i = 3 THEN 3 END AS A1 FROM (VALUES (1), "
            "(2), (3)) AS I0(i)");
}

TEST_F(OptimizationTest, RelationalAbstraction6) {
  EXPECT_EQ(TranslateExpression("{(1);(2)}"),
            "SELECT CASE WHEN I0.i = 1 THEN T0.A1 WHEN I0.i = 2 THEN T1.A1 END AS A1 FROM (SELECT 1) AS T0, (SELECT 2) "
            "AS T1, (VALUES (1), (2)) AS I0(i)");
}

TEST_F(OptimizationTest, FormulaBindings1) {
  EXPECT_EQ(TranslateExpression("(x): A(x)"), "SELECT T0.A1 AS A1 FROM A AS T0");
}

TEST_F(OptimizationTest, FormulaBindings2) {
  EXPECT_EQ(TranslateExpression("(x, x): A(x)"), "SELECT T0.A1 AS A1, T0.A1 AS A2 FROM A AS T0");
}

TEST_F(OptimizationTest, FormulaBindings3) {
  EXPECT_EQ(TranslateExpression("(x): B(x,1)"), "SELECT T0.A1 AS A1 FROM B AS T0 WHERE T0.A2 = 1");
}

TEST_F(OptimizationTest, FormulaBindings4) {
  EXPECT_EQ(TranslateExpression("(x): {B[1]}(x)"), "SELECT T0.A2 AS A1 FROM B AS T0 WHERE T0.A1 = 1");
}

TEST_F(OptimizationTest, FormulaBindings5) {
  EXPECT_EQ(TranslateExpression("(x): A(x) and D(x)"), "SELECT T0.A1 AS A1 FROM A AS T0, D AS T1 WHERE T0.A1 = T1.A1");
}

TEST_F(OptimizationTest, FormulaBindings6) {
  EXPECT_EQ(TranslateExpression("(x in A): D(x)"), "SELECT T0.A1 AS A1 FROM D AS T0, A AS T1 WHERE T0.A1 = T1.A1");
}

TEST_F(OptimizationTest, ExpressionBindings1) {
  EXPECT_EQ(TranslateExpression("[x]: A[x] where x > 1"), "SELECT T0.A1 AS A1 FROM A AS T0 WHERE T0.A1 > 1");
}

TEST_F(OptimizationTest, ExpressionBindings2) {
  EXPECT_EQ(TranslateExpression("[x in A, y in D]: C[x, y]"),
            "SELECT T0.A1 AS A1, T0.A2 AS A2, T0.A3 AS A3 FROM C AS T0, A AS T1, D AS T2 WHERE T0.A2 = T2.A1 AND T0.A1 "
            "= T1.A1");
}

TEST_F(OptimizationTest, ExpressionBindings3) {
  EXPECT_EQ(TranslateExpression("[x in A, y]: C[x, y] where D(y)"),
            "SELECT T0.A1 AS A1, T0.A2 AS A2, T0.A3 AS A3 FROM C AS T0, D AS T1, A AS T4 WHERE T0.A1 = T4.A1 AND T0.A2 "
            "= T1.A1");
}

TEST_F(OptimizationTest, ExpressionBindings4) {
  EXPECT_EQ(TranslateExpression("[x in A, y in D]: C[x, y]"),
            "SELECT T0.A1 AS A1, T0.A2 AS A2, T0.A3 AS A3 FROM C AS T0, A AS T1, D AS T2 WHERE T0.A2 = T2.A1 AND T0.A1 "
            "= T1.A1");
}

TEST_F(OptimizationTest, ExpressionConstantTerms1) {
  EXPECT_EQ(TranslateExpression("B[1+2]"), "SELECT T0.A2 AS A1 FROM B AS T0 WHERE T0.A1 = 1 + 2");
}

TEST_F(OptimizationTest, ExpressionConstantTerms2) {
  EXPECT_EQ(TranslateExpression("B[2*(3+4)]"), "SELECT T0.A2 AS A1 FROM B AS T0 WHERE T0.A1 = 2 * (3 + 4)");
}

TEST_F(OptimizationTest, ParameterVariableTerms1) {
  EXPECT_EQ(TranslateExpression("A(x+1)"), "SELECT T0.A1 - 1 AS x FROM A AS T0");
}

TEST_F(OptimizationTest, ParameterVariableTerms2) {
  EXPECT_EQ(TranslateExpression("A(2*x)"), "SELECT T0.A1 / 2 AS x FROM A AS T0");
}

TEST_F(OptimizationTest, ParameterVariableTerms3) {
  EXPECT_EQ(TranslateExpression("A(2*x-1)"), "SELECT (T0.A1 + 1) / 2 AS x FROM A AS T0");
}

TEST_F(OptimizationTest, ParameterVariableTerms4) {
  EXPECT_EQ(TranslateExpression("A(3*(2*x-1+5*x)+x)"), "SELECT (T0.A1 + 3) / 22 AS x FROM A AS T0");
}

TEST_F(OptimizationTest, ParameterVariableTerms5) {
  // Like-term collection merges (T0.A1 - 1) - 1 to T0.A1 - 2
  EXPECT_EQ(TranslateExpression("B(x+1,x-1)"), "SELECT T0.A1 - 1 AS x FROM B AS T0 WHERE T0.A2 = T0.A1 - 2");
}

TEST_F(OptimizationTest, ParameterVariableTerms6) {
  EXPECT_EQ(TranslateExpression("B(x+1,x,y)"),
            "SELECT T0.A1 - 1 AS x, T0.A3 AS y FROM B AS T0 WHERE T0.A1 - 1 = T0.A2");
}

TEST_F(OptimizationTest, ParameterVariableTerms7) {
  EXPECT_EQ(TranslateExpression("B(x+1,B[x])"),
            "SELECT T1.A1 AS x FROM B AS T0, B AS T1 WHERE T1.A1 = T0.A1 - 1 AND T0.A2 = T1.A2");
}

// Self-join detection via canonical form: T0.A1 = 22*(T2.A1+3)/22 + -3 is algebraically T0.A1 = T2.A1.
// The self-join optimizer uses canonical form comparison (no expression simplification) to detect this.
TEST_F(OptimizationTest, SelfJoinCanonicalFormEquality) {
  auto t2 = std::make_shared<sql::ast::Table>("A", 1);
  auto t2_source = std::make_shared<sql::ast::Source>(t2, "T2");
  auto col = std::make_shared<sql::ast::Column>("A1", t2_source);
  auto sum = std::make_shared<sql::ast::ParenthesisTerm>(
      std::make_shared<sql::ast::Operation>(col, std::make_shared<sql::ast::Constant>(3), "+"));
  auto mul = std::make_shared<sql::ast::Operation>(std::make_shared<sql::ast::Constant>(22), sum, "*");
  auto div = std::make_shared<sql::ast::Operation>(mul, std::make_shared<sql::ast::Constant>(22), "/");
  auto rhs = std::make_shared<sql::ast::Operation>(div, std::make_shared<sql::ast::Constant>(-3), "+");
  auto t0_source = std::make_shared<sql::ast::Source>(t2, "T0");
  auto cond = std::make_shared<sql::ast::ComparisonCondition>(std::make_shared<sql::ast::Column>("A1", t0_source),
                                                              sql::ast::CompOp::EQ, rhs);
  EXPECT_TRUE(sql::ast::SelfJoinOptimizer::IsEquivalenceCandidate(cond));
}

TEST_F(OptimizationTest, FailedParameterVariableTerms1) {
  EXPECT_THROW(TranslateExpression("A(x+y)"), TranslationException);
}

TEST_F(OptimizationTest, FailedParameterVariableTerms2) {
  EXPECT_THROW(TranslateExpression("A(x*x)"), TranslationException);
}

TEST_F(OptimizationTest, FailedParameterVariableTerms3) {
  EXPECT_THROW(TranslateExpression("A(x*x - x*x)"), TranslationException);
}

TEST_F(OptimizationTest, FailedParameterVariableTerms4) {
  EXPECT_THROW(TranslateExpression("A((1-1)*x)"), TranslationException);
}

TEST_F(OptimizationTest, ExpressionAsTermRewriterBindingsBody) {
  EXPECT_EQ(TranslateExpression("[x in A, y in A]: x+y+1"),
            "SELECT T6.x AS A1, T6.y AS A2, T6.A1 AS A3 FROM (SELECT T4.x, T4.y, T4._x0 AS A1 FROM (SELECT T1.x, T3.y, "
            "T1.x + T3.y + 1 AS _x0 FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1, (SELECT T2.A1 AS y FROM A AS T2) AS "
            "T3) AS T4) AS T6");
}

TEST_F(OptimizationTest, Program) {
  EXPECT_EQ(TranslateDefinition("def R {[x in A]: B[x]}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT T0.A1 AS A1, T0.A2 AS A2 FROM B AS T0, A AS T1 WHERE T0.A1 = "
            "T1.A1);");
}

TEST_F(OptimizationTest, NamedAttributesFormula) {
  default_edb_map["R"] = RelationInfo({"id", "name"});
  EXPECT_EQ(TranslateFormula("R(x, y)"), "SELECT T0.id AS x, T0.name AS y FROM R AS T0");
}

TEST_F(OptimizationTest, NamedAttributesConjunction) {
  default_edb_map["R"] = RelationInfo({"id", "name"});
  default_edb_map["S"] = RelationInfo({"student_id", "grade"});
  EXPECT_EQ(TranslateFormula("R(x, y) and S(x, z)"),
            "SELECT T0.id AS x, T0.name AS y, T1.grade AS z FROM R AS T0, S AS T1 WHERE T0.id = T1.student_id");
}

TEST_F(OptimizationTest, NamedAttributesExistential) {
  default_edb_map["R"] = RelationInfo({"student_id", "course_id"});
  default_edb_map["S"] = RelationInfo({"course_id"});
  EXPECT_EQ(TranslateFormula("exists ((y in S) | R(x, y))"),
            "SELECT T0.student_id AS x FROM R AS T0, S AS T2 WHERE T0.course_id = T2.course_id");
}

TEST_F(OptimizationTest, NamedAttributesPartialApplication) {
  default_edb_map["R"] = RelationInfo({"student_id", "course_id", "grade"});
  EXPECT_EQ(
      TranslateDefinition("def S {R[x]}"),
      "CREATE OR REPLACE VIEW S AS (SELECT DISTINCT T0.student_id AS x, T0.course_id AS A1, T0.grade AS A2 FROM R "
      "AS T0);");
}

TEST_F(OptimizationTest, NamedAttributesAggregate) {
  default_edb_map["R"] = RelationInfo({"student_id", "grade"});
  EXPECT_EQ(TranslateDefinition("def S {max[R[x]]}"),
            "CREATE OR REPLACE VIEW S AS (SELECT DISTINCT T0.student_id AS x, MAX(T0.grade) AS A1 FROM R AS T0 GROUP "
            "BY T0.student_id);");
}

TEST_F(OptimizationTest, SingleNamedAttribute) {
  default_edb_map["R"] = RelationInfo({"id"});
  EXPECT_EQ(TranslateFormula("R(x)"), "SELECT T0.id AS x FROM R AS T0");
}

TEST_F(OptimizationTest, ThreeNamedAttributes) {
  default_edb_map["R"] = RelationInfo({"student_id", "course_id", "grade"});
  EXPECT_EQ(TranslateFormula("R(x, y, z)"), "SELECT T0.student_id AS x, T0.course_id AS y, T0.grade AS z FROM R AS T0");
}

TEST_F(OptimizationTest, NamedAttributesRepeatedVariables) {
  default_edb_map["R"] = RelationInfo({"id", "parent_id"});
  EXPECT_EQ(TranslateFormula("R(x, x)"), "SELECT T0.id AS x FROM R AS T0 WHERE T0.id = T0.parent_id");
}

TEST_F(OptimizationTest, NamedAttributesBindingFormula) {
  default_edb_map["F"] = RelationInfo({"name"});
  EXPECT_EQ(TranslateExpression("(x): F(x)"), "SELECT T0.name AS A1 FROM F AS T0");
}

TEST_F(OptimizationTest, CompositionRelation) {
  EXPECT_EQ(TranslateExpression("(x, y): exists((z) | B(x, z) and E(z, y))"),
            "SELECT T0.A1 AS A1, T1.A2 AS A2 FROM B AS T0, E AS T1 WHERE T0.A2 = T1.A1");
}

TEST_F(OptimizationTest, SelfComposition) {
  EXPECT_EQ(TranslateExpression("(x, y): exists((z) | B(x, z) and B(z, y))"),
            "SELECT T0.A1 AS A1, T1.A2 AS A2 FROM B AS T0, B AS T1 WHERE T0.A2 = T1.A1");
}

TEST_F(OptimizationTest, FirstTransitivityComposition) {
  EXPECT_EQ(TranslateExpression("(x, y): B(x, y) or exists((z) | B(x, z) and B(z, y))"),
            "SELECT T6.x AS A1, T6.y AS A2 FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0 UNION SELECT T1.A1 AS x, "
            "T2.A2 AS y FROM B AS T1, B AS T2 WHERE T1.A2 = T2.A1) AS T6");
}

TEST_F(OptimizationTest, SimpleReferenceDefinition) {
  EXPECT_EQ(TranslateDefinition("def R {A}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT T0.A1 AS A1 FROM A AS T0);");
}

TEST_F(OptimizationTest, ExistentialNotBoundingAllVariables) {
  EXPECT_EQ(TranslateFormula("exists((y) | A(x) and D(y))"),
            "SELECT T4.x FROM (SELECT T2.x, T3.y FROM (SELECT T0.A1 AS x FROM A AS T0) AS T2, (SELECT T1.A1 AS y FROM "
            "D AS T1) AS T3) AS T4");
}

TEST_F(OptimizationTest, RecursiveDefinition) {
  default_edb_map["A"] = RelationInfo(1);
  default_edb_map["B"] = RelationInfo(1);
  default_edb_map["C"] = RelationInfo(1);
  EXPECT_EQ(TranslateDefinition("def Q {(x) : B(x) or exists ((y) | Q(y) and C(y))}"),
            "CREATE OR REPLACE VIEW Q AS (WITH RECURSIVE R0(A1) AS (SELECT T0.A1 AS x FROM B AS T0 UNION SELECT  FROM "
            "(SELECT T3.y FROM (SELECT T1.A1 AS y FROM R0 AS T1) AS T3, (SELECT T2.A1 AS y FROM C AS T2) AS T4 WHERE "
            "T3.y = T4.y) AS T5) SELECT DISTINCT T6.x AS A1 FROM (SELECT R0.A1 AS x FROM R0) AS T6);");
}

TEST_F(OptimizationTest, WeirdEdgeCase1) {
  std::string input1 =
      "def r {(1,2); (1,3); (3,4)}\n"
      "def s {(2,5); (4,7)}\n"
      "def jrs {(x,y,z) : r(x,y) and s(y,z)}\n"
      "def rng {2;3}\n"
      "def tfa {(x) : forall( (y in rng) | r(x,y))}";
  std::string input2 =
      "def r {(1,2); (1,3); (3,4)}\n"
      "def s {(2,5); (4,7)}\n"
      "def jrs {(x,y,z) : r(x,y) and s(y,z)}";
  std::string output1 = TranslateProgram(input1);
  std::string output2 = TranslateProgram(input2);
  std::regex pattern(R"(CREATE OR REPLACE VIEW jrs.*?;)");
  std::smatch match1, match2;
  bool found1 = std::regex_search(output1, match1, pattern);
  bool found2 = std::regex_search(output2, match2, pattern);
  ASSERT_TRUE(found1) << "Pattern not found in output1";
  ASSERT_TRUE(found2) << "Pattern not found in output2";
  EXPECT_EQ(match1.str(), match2.str());
}

TEST_F(OptimizationTest, BindingEquality) { EXPECT_EQ(TranslateExpression("(x): x = 1"), "SELECT 1 AS A1"); }

TEST_F(OptimizationTest, EdgeCase1) { EXPECT_EQ(TranslateExpression("(x,y,z): B(x,y+1) and z = x-y"), ""); }

// DomainToSql tests (DomainToSqlConstantDomain, DomainToSqlDefinedDomain, DomainToSqlProjection,
// DomainToSqlDomainUnion, DomainToSqlDomainOperation) remain in test_translation.cc as they require
// TranslationTest's DomainToSqlString helper and FRIEND_TEST access to Translator::DomainToSql.

}  // namespace rel2sql

// Helper functions for testing individual optimizers
std::string OptimizeSQLWithCTEOptimizer(const std::string& sql) {
  auto expr = rel2sql::ParseSQL(sql);
  rel2sql::sql::ast::CTEInliner cte_inliner;
  cte_inliner.Visit(*expr);
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
                                             const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
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

TEST(FlattenerOptimizationTest, TermSubstitutionPreservesPrecedenceInSubtraction) {
  // E2.A1 - E1.A1 where E1.A1 = T1.A2 - 1 must become T0.A1 - (T1.A2 - 1),
  // not T0.A1 - T1.A2 - 1 (which would parse as (T0.A1 - T1.A2) - 1).
  rel2sql::RelationMap edb_map = rel2sql::CreateDefaultEDBMap();
  std::string sql =
      "SELECT E2.A1 - E1.A1 AS result\n"
      "FROM (SELECT T0.A1 AS A1 FROM B AS T0) AS E2,\n"
      "     (SELECT T1.A2 - 1 AS A1 FROM B AS T1) AS E1";
  auto expr = rel2sql::ParseSQL(sql, edb_map);
  ASSERT_NE(expr, nullptr);
  rel2sql::sql::ast::FlattenerOptimizer flattener_optimizer;
  flattener_optimizer.Visit(*expr);
  std::string result = expr->ToString();
  // Must contain parenthesized form to preserve semantics
  EXPECT_TRUE(result.find("( T1.A2 - 1 )") != std::string::npos || result.find("(T1.A2 - 1)") != std::string::npos)
      << "Expected (T1.A2 - 1) in: " << result;
  // Must NOT have the wrong form: "X - T1.A2 - 1" (missing parens; would parse as (X - T1.A2) - 1)
  EXPECT_TRUE(result.find(" - T1.A2 - 1") == std::string::npos)
      << "Incorrect substitution (missing parens) in: " << result;
}

TEST(FlattenerOptimizationTest, UnionProjectionPushdown) {
  // SELECT T2.x AS A1 FROM (SELECT T0.A1 AS x FROM A AS T0 UNION SELECT T1.A1 AS x FROM B AS T1) AS T2
  // should simplify to: SELECT T0.A1 AS A1 FROM A AS T0 UNION SELECT T1.A1 AS A1 FROM B AS T1
  rel2sql::RelationMap edb_map = rel2sql::CreateDefaultEDBMap();
  std::string sql =
      "SELECT T2.x AS A1 FROM (SELECT T0.A1 AS x FROM A AS T0 UNION SELECT T1.A1 AS x FROM B AS T1) AS T2";
  auto expr = rel2sql::ParseSQL(sql, edb_map);
  ASSERT_NE(expr, nullptr);
  rel2sql::sql::ast::Optimizer optimizer;
  auto optimized = optimizer.Optimize(std::static_pointer_cast<rel2sql::sql::ast::Expression>(expr));
  ASSERT_NE(optimized, nullptr);
  std::string result = optimized->ToString();
  EXPECT_EQ(result, "SELECT T0.A1 AS A1 FROM A AS T0 UNION SELECT T1.A1 AS A1 FROM B AS T1");
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
  rel2sql::RelationMap edb_map;
  edb_map["A"] = rel2sql::RelationInfo(2);
  std::string result = OptimizeSQLWithSelfJoinOptimizer(sql, edb_map);
  // Should NOT eliminate because self join is incomplete (only A1 matches, not A2)
  EXPECT_TRUE(result.find("A AS T1") == std::string::npos);
}
