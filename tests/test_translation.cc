// cspell:ignore GTEST
#include <gtest/gtest.h>

#include <regex>

#include "RelParser.h"
#include "api/translate.h"
#include "preprocessing/preprocessor.h"
#include "rel_ast/extended_ast.h"
#include "rel_ast/relation_info.h"
#include "support/exceptions.h"
#include "test_common.h"

using psr = rel_parser::RelParser;

namespace rel2sql {

std::string TranslateWithoutOptimization(antlr4::ParserRuleContext* tree,
                                         const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  Preprocessor preprocessor(edb_map);
  auto ast = preprocessor.Process(tree);

  auto sql = GetSQLFromAST(ast);

  return sql->ToString();
}

class TranslationTest : public ::testing::Test {
 protected:
  void SetUp() override { default_edb_map = CreateDefaultEDBMap(); }

  std::string TranslateFormula(const std::string& input) {
    auto parser = GetParser(input);
    auto tree = parser->formula();
    return TranslateWithoutOptimization(tree, default_edb_map);
  }

  std::string TranslateExpression(const std::string& input) {
    auto parser = GetParser(input);
    auto tree = parser->expr();
    return TranslateWithoutOptimization(tree, default_edb_map);
  }

  std::string TranslateProgram(const std::string& input) {
    auto parser = GetParser(input);
    auto tree = parser->program();
    return TranslateWithoutOptimization(tree, default_edb_map);
  }

  std::string TranslateDefinition(const std::string& input) {
    auto parser = GetParser(input);
    auto tree = parser->relDef();
    return TranslateWithoutOptimization(tree, default_edb_map);
  }

  rel2sql::RelationMap default_edb_map;
};

TEST_F(TranslationTest, FullApplication1) { EXPECT_EQ(TranslateFormula("A(x)"), "SELECT T0.A1 AS x FROM A AS T0"); }

TEST_F(TranslationTest, FullApplication2) {
  EXPECT_EQ(TranslateFormula("B(x, y)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0");
}

TEST_F(TranslationTest, FullApplication3) {
  EXPECT_EQ(TranslateFormula("C(x, y, z)"), "SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0");
}

TEST_F(TranslationTest, FullApplication4) {
  EXPECT_EQ(TranslateFormula("B(x, x)"), "SELECT T0.A1 AS x FROM B AS T0 WHERE T0.A1 = T0.A2");
}

TEST_F(TranslationTest, FullApplication5) {
  EXPECT_EQ(TranslateFormula("C(x, x, x)"), "SELECT T0.A1 AS x FROM C AS T0 WHERE T0.A1 = T0.A2 AND T0.A2 = T0.A3");
}

TEST_F(TranslationTest, FullApplication6) {
  EXPECT_EQ(TranslateFormula("C(x, y, x)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM C AS T0 WHERE T0.A1 = T0.A3");
}

TEST_F(TranslationTest, OperatorFormula) {
  EXPECT_EQ(TranslateFormula("A(x) and x*x > 5"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x * T1.x > 5");
}

TEST_F(TranslationTest, ComparisonOperators1) {
  EXPECT_EQ(TranslateFormula("A(x) and x < 5"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x < 5");
}

TEST_F(TranslationTest, ComparisonOperators2) {
  EXPECT_EQ(TranslateFormula("A(x) and x <= 5"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x <= 5");
}

TEST_F(TranslationTest, ComparisonOperators3) {
  EXPECT_EQ(TranslateFormula("A(x) and x >= 5"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x >= 5");
}

TEST_F(TranslationTest, ComparisonOperators4) {
  EXPECT_EQ(TranslateFormula("A(x) and x = 5"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x = 5");
}

TEST_F(TranslationTest, ComparisonOperators5) {
  EXPECT_EQ(TranslateFormula("A(x) and x != 5"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x != 5");
}

TEST_F(TranslationTest, ArithmeticInComparisons1) {
  EXPECT_EQ(TranslateFormula("B(x,y) and x + y > 5"),
            "SELECT T1.x, T1.y FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0) AS T1 WHERE T1.x + T1.y > 5");
}

TEST_F(TranslationTest, ArithmeticInComparisons2) {
  EXPECT_EQ(TranslateFormula("B(x,y) and x - y < 0"),
            "SELECT T1.x, T1.y FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0) AS T1 WHERE T1.x - T1.y < 0");
}

TEST_F(TranslationTest, ArithmeticInComparisons3) {
  EXPECT_EQ(TranslateFormula("B(x,y) and x * y = 10"),
            "SELECT T1.x, T1.y FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0) AS T1 WHERE T1.x * T1.y = 10");
}

TEST_F(TranslationTest, ArithmeticInComparisons4) {
  EXPECT_EQ(TranslateFormula("B(x,y) and x / y > 2"),
            "SELECT T1.x, T1.y FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0) AS T1 WHERE T1.x / T1.y > 2");
}

TEST_F(TranslationTest, ArithmeticInComparisons5) {
  EXPECT_EQ(TranslateFormula("C(x,y,z) and (x + y) * z > 100"),
            "SELECT T1.x, T1.y, T1.z FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0) AS T1 WHERE (x + y) "
            "* T1.z > 100");
}

TEST_F(TranslationTest, ArithmeticInComparisons6) {
  EXPECT_EQ(TranslateFormula("C(x,y,z) and x + y + z = 15"),
            "SELECT T1.x, T1.y, T1.z FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0) AS T1 WHERE T1.x + "
            "T1.y + T1.z = 15");
}

TEST_F(TranslationTest, NegativeLiteral1) {
  EXPECT_EQ(TranslateFormula("A(x) and x > -5"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x > -5");
}

TEST_F(TranslationTest, NegativeLiteral2) {
  EXPECT_EQ(TranslateFormula("A(x) and x >= -10.5"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x >= -10.5");
}

TEST_F(TranslationTest, NegativeLiteral3) {
  EXPECT_EQ(TranslateFormula("A(x) and x = -1"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x = -1");
}

TEST_F(TranslationTest, FloatLiteral) {
  EXPECT_EQ(TranslateFormula("A(x) and x < 3.14"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x < 3.14");
}

TEST_F(TranslationTest, ConjunctionFormula) {
  EXPECT_EQ(TranslateFormula("A(x) and D(x)"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1, (SELECT T2.A1 AS x FROM D AS T2) AS T3 WHERE "
            "T1.x = T3.x");
}

TEST_F(TranslationTest, DisjunctionFormula) {
  EXPECT_EQ(TranslateFormula("A(x) or D(x)"),
            "SELECT T2.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T2 UNION SELECT T3.x FROM (SELECT T1.A1 AS x FROM D "
            "AS T1) AS T3");
}

TEST_F(TranslationTest, DisjunctionFormula2) {
  EXPECT_EQ(TranslateFormula("A(x) or (D(x) and G(x))"),
            "SELECT T5.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T5 UNION SELECT T6.x FROM (SELECT T2.x FROM (SELECT "
            "T1.A1 AS x FROM D AS T1) AS T2, (SELECT T3.A1 AS x FROM G AS T3) AS T4 WHERE T2.x = T4.x) AS T6");
}

TEST_F(TranslationTest, NegationFormula1) {
  EXPECT_EQ(
      TranslateFormula("A(x) and not D(x)"),
      "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x NOT IN (SELECT T2.A1 AS x FROM D AS T2)");
}

TEST_F(TranslationTest, NegationFormula2) {
  EXPECT_EQ(
      TranslateFormula("not A(x) and D(x)"),
      "SELECT T1.x FROM (SELECT T0.A1 AS x FROM D AS T0) AS T1 WHERE T1.x NOT IN (SELECT T2.A1 AS x FROM A AS T2)");
}

TEST_F(TranslationTest, NegationFormula3) {
  EXPECT_EQ(TranslateFormula("A(x) and not (D(x) or G(x))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x NOT IN (SELECT T4.x FROM (SELECT T2.A1 "
            "AS x FROM D AS T2) AS T4 UNION SELECT T5.x FROM (SELECT T3.A1 AS x FROM G AS T3) AS T5)");
}

TEST_F(TranslationTest, NegationFormula4) {
  EXPECT_EQ(TranslateFormula("A(x) and D(y) and not D(x) and not A(y)"),
            "SELECT T1.x, T3.y FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1, (SELECT T2.A1 AS y FROM D AS T2) AS T3 "
            "WHERE T1.x NOT IN (SELECT T4.A1 AS x FROM D AS T4) AND T3.y NOT IN (SELECT T5.A1 AS y FROM A AS T5)");
}

TEST_F(TranslationTest, ExistentialFormula1) {
  EXPECT_EQ(TranslateFormula("exists ((y) | B(x, y))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0) AS T1");
}

TEST_F(TranslationTest, ExistentialFormula2) {
  EXPECT_EQ(TranslateFormula("exists ((y, z) | C(x, y, z))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0) AS T1");
}

TEST_F(TranslationTest, ExistentialFormula3) {
  EXPECT_EQ(TranslateFormula("exists ((y in A) | B(x, y))"),
            "SELECT T2.x FROM (SELECT T1.A1 AS x, T1.A2 AS y FROM B AS T1) AS T2, A AS T0 WHERE T2.y = T0.A1");
}

TEST_F(TranslationTest, ExistentialFormula4) {
  EXPECT_EQ(TranslateFormula("exists ((y in A, z in D) | C(x, y, z))"),
            "SELECT T3.x FROM (SELECT T2.A1 AS x, T2.A2 AS y, T2.A3 AS z FROM C AS T2) AS T3, A AS T0, D AS T1 WHERE "
            "T3.y = T0.A1 AND T3.z = T1.A1");
}

TEST_F(TranslationTest, ExistentialFormula5) {
  EXPECT_EQ(
      TranslateFormula("exists ((y in A, z) | C(x, y, z))"),
      "SELECT T2.x FROM (SELECT T1.A1 AS x, T1.A2 AS y, T1.A3 AS z FROM C AS T1) AS T2, A AS T0 WHERE T2.y = T0.A1");
}

TEST_F(TranslationTest, NestedQuantifiers1) {
  EXPECT_EQ(
      TranslateFormula("exists ((y) | exists ((z) | C(x, y, z)))"),
      "SELECT T2.x FROM (SELECT T1.x, T1.y FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0) AS T1) AS "
      "T2");
}

TEST_F(TranslationTest, NestedQuantifiers2) {
  EXPECT_EQ(
      TranslateFormula("exists ((y in A) | forall ((z in D) | C(x, y, z)))"),
      "SELECT T4.x FROM (SELECT T3.x, T3.y FROM (SELECT T2.A1 AS x, T2.A2 AS y, T2.A3 AS z FROM C AS T2) AS T3 WHERE "
      "NOT EXISTS (SELECT * FROM D AS T1 WHERE (T3.x, T3.y, T1.A1) NOT IN (SELECT * FROM (SELECT T2.A1 AS x, T2.A2 AS "
      "y, T2.A3 AS z FROM C AS T2) AS T3))) AS T4, A AS T0 WHERE T4.y = T0.A1");
}

TEST_F(TranslationTest, NestedQuantifiers3) {
  EXPECT_EQ(TranslateFormula("forall ((y in A) | exists ((z) | C(x, y, z)))"),
            "SELECT T3.x FROM (SELECT T2.x, T2.y FROM (SELECT T1.A1 AS x, T1.A2 AS y, T1.A3 AS z FROM C AS T1) AS T2) "
            "AS T3 WHERE NOT EXISTS (SELECT * FROM A AS T0 WHERE (T3.x, T0.A1) NOT IN (SELECT * FROM (SELECT T2.x, "
            "T2.y FROM (SELECT T1.A1 AS x, T1.A2 AS y, T1.A3 AS z FROM C AS T1) AS T2) AS T3))");
}

TEST_F(TranslationTest, NestedQuantifiers4) {
  EXPECT_EQ(TranslateFormula("exists ((y) | exists ((z) | exists ((w) | I(x, y, z) and I(y, z, w))))"),
            "SELECT T6.x FROM (SELECT T5.x, T5.y FROM (SELECT T4.x, T4.y, T4.z FROM (SELECT T1.x, T1.y, T1.z, T3.w "
            "FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM I AS T0) AS T1, (SELECT T2.A1 AS y, T2.A2 AS z, "
            "T2.A3 AS w FROM I AS T2) AS T3 WHERE T1.z = T3.z AND T1.y = T3.y) AS T4) AS T5) AS T6");
}

TEST_F(TranslationTest, UniversalFormula1) {
  // TODO: Must remove inner-most FROM subquery alias (final "AS T1")
  EXPECT_EQ(
      TranslateFormula("forall ((y in A) | B(x, y))"),
      "SELECT T2.x FROM (SELECT T1.A1 AS x, T1.A2 AS y FROM B AS T1) AS T2 WHERE NOT EXISTS (SELECT * FROM A AS T0 "
      "WHERE (T2.x, T0.A1) NOT IN (SELECT * FROM (SELECT T1.A1 AS x, T1.A2 AS y FROM B AS T1) AS T2))");
}

TEST_F(TranslationTest, UniversalFormula2) {
  EXPECT_EQ(
      TranslateFormula("forall ((y in A, z in D) | C(x, y, z))"),
      "SELECT T3.x FROM (SELECT T2.A1 AS x, T2.A2 AS y, T2.A3 AS z FROM C AS T2) AS T3 WHERE NOT EXISTS (SELECT * "
      "FROM A AS T0, D AS T1 WHERE (T3.x, T0.A1, T1.A1) NOT IN (SELECT * FROM (SELECT T2.A1 AS x, T2.A2 AS y, "
      "T2.A3 AS z FROM C AS T2) AS T3))");
}

TEST_F(TranslationTest, ProductExpression) { EXPECT_EQ(TranslateExpression("(1, 2)"), "SELECT 1, 2"); }

TEST_F(TranslationTest, Conditional1) {
  EXPECT_EQ(TranslateExpression("B[x] where A(x)"),
            "SELECT T2.x, T2.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0) AS T2, (SELECT T1.A1 AS x FROM A AS "
            "T1) AS T3 WHERE T2.x = T3.x");
}

TEST_F(TranslationTest, Conditional2) {
  EXPECT_EQ(TranslateExpression("B[x] where x > 1"),
            "SELECT T1.x, T1.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0) AS T1 WHERE T1.x > 1");
}

TEST_F(TranslationTest, ConditionalComparatorConjunction) {
  EXPECT_EQ(TranslateExpression("B[x] where (x > 1 and x < 5)"),
            "SELECT T1.x, T1.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0) AS T1 WHERE T1.x > 1 AND T1.x < 5");
}

TEST_F(TranslationTest, ConditionalComparatorConjunctionInvalidVar) {
  EXPECT_THROW(TranslateExpression("B[x] where (y > 1 and x > 0)"), VariableException);
}

TEST_F(TranslationTest, NestedConditional1) {
  EXPECT_EQ(TranslateExpression("(B[x] where A(x)) where D(x)"),
            "SELECT T6.x, T6.A1 FROM (SELECT T4.x, T4.A1 AS A1 FROM (SELECT T2.x, T2.A1 FROM (SELECT T0.A1 AS x, T0.A2 "
            "AS A1 FROM B AS T0) AS T2, (SELECT T1.A1 AS x FROM A AS T1) AS T3 WHERE T2.x = T3.x) AS T4) AS T6, "
            "(SELECT T5.A1 AS x FROM D AS T5) AS T7 WHERE T6.x = T7.x");
}

TEST_F(TranslationTest, NestedConditional2) {
  EXPECT_EQ(
      TranslateExpression("B[x] where (A(x) and D(x))"),
      "SELECT T5.x, T5.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0) AS T5, (SELECT T2.x FROM (SELECT T1.A1 AS "
      "x FROM A AS T1) AS T2, (SELECT T3.A1 AS x FROM D AS T3) AS T4 WHERE T2.x = T4.x) AS T6 WHERE T5.x = T6.x");
}

TEST_F(TranslationTest, PartialApplication1) {
  EXPECT_EQ(TranslateExpression("B[x]"), "SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0");
}

TEST_F(TranslationTest, PartialApplication2) {
  EXPECT_EQ(TranslateExpression("B[1]"), "SELECT T0.A2 AS A1 FROM B AS T0, (SELECT 1 AS A1) AS T1 WHERE T0.A1 = T1.A1");
}

TEST_F(TranslationTest, NestedPartialApplication1) {
  EXPECT_EQ(
      TranslateExpression("B[E[x]]"),
      "SELECT T2.x, T0.A2 AS A1 FROM B AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM E AS T1) AS T2 WHERE T0.A1 = T2.A1");
}

TEST_F(TranslationTest, NestedPartialApplication2) {
  EXPECT_EQ(TranslateExpression("B[E[B[x]]]"),
            "SELECT T4.x, T0.A2 AS A1 FROM B AS T0, (SELECT T3.x, T1.A2 AS A1 FROM E AS T1, (SELECT T2.A1 AS x, T2.A2 "
            "AS A1 FROM B AS T2) AS T3 WHERE T1.A1 = T3.A1) AS T4 WHERE T0.A1 = T4.A1");
}

TEST_F(TranslationTest, PartialApplicationMixedParams1) {
  EXPECT_EQ(TranslateExpression("C[B[x], y]"),
            "SELECT T2.x, T0.A2 AS y, T0.A3 AS A1 FROM C AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM B AS T1) AS T2 "
            "WHERE T0.A1 = T2.A1");
}

TEST_F(TranslationTest, PartialApplicationMixedParams2) {
  EXPECT_EQ(TranslateExpression("C[x, 1]"),
            "SELECT T0.A1 AS x, T0.A3 AS A1 FROM C AS T0, (SELECT 1 AS A1) AS T1 WHERE T0.A2 = T1.A1");
}

TEST_F(TranslationTest, PartialApplicationMixedParams3) {
  EXPECT_EQ(TranslateExpression("C[B[x], E[y]]"),
            "SELECT T2.x, T4.y, T0.A3 AS A1 FROM C AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM B AS T1) AS T2, (SELECT "
            "T3.A1 AS y, T3.A2 AS A1 FROM E AS T3) AS T4 WHERE T0.A1 = T2.A1 AND T0.A2 = T4.A1");
}

TEST_F(TranslationTest, PartialApplicationSharingVariables1) {
  EXPECT_EQ(TranslateExpression("C[B[x], E[x]]"),
            "SELECT T2.x, T0.A3 AS A1 FROM C AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM B AS T1) AS T2, (SELECT T3.A1 "
            "AS x, T3.A2 AS A1 FROM E AS T3) AS T4 WHERE T0.A1 = T2.A1 AND T0.A2 = T4.A1 AND T2.x = T4.x");
}

TEST_F(TranslationTest, PartialApplicationSharingVariables2) {
  EXPECT_EQ(TranslateExpression("I[C[x, y], F[y, z]]"),
            "SELECT T2.x, T2.y, T4.z, T0.A3 AS A1 FROM I AS T0, (SELECT T1.A1 AS x, T1.A2 AS y, T1.A3 AS A1 FROM C AS "
            "T1) AS T2, (SELECT T3.A1 AS y, T3.A2 AS z, T3.A3 AS A1 FROM F AS T3) AS T4 WHERE T0.A1 = T2.A1 AND T0.A2 "
            "= T4.A1 AND T2.y = T4.y");
}

TEST_F(TranslationTest, PartialApplicationSharingVariables3) {
  EXPECT_EQ(TranslateExpression("C[B[x], x]"),
            "SELECT T2.x, T0.A3 AS A1 FROM C AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM B AS T1) AS T2 WHERE T2.x = "
            "T0.A2 AND T0.A1 = T2.A1");
}

TEST_F(TranslationTest, PartialApplicationSharingVariables4) {
  EXPECT_EQ(TranslateExpression("C[B[x], x, y]"),
            "SELECT T2.x, T0.A3 AS y FROM C AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM B AS T1) AS T2 WHERE T2.x = "
            "T0.A2 AND T0.A1 = T2.A1");
}

TEST_F(TranslationTest, DISABLED_PartialApplicationOnExpression1) {
  EXPECT_EQ(TranslateExpression("{C[x]}[x]"),
            "SELECT T2.x, T2.A2 AS A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1, T0.A3 AS A2 FROM C AS T0) AS T2 WHERE T2.x "
            "= T2.A1");
}

TEST_F(TranslationTest, PartialApplicationOnExpression2) {
  EXPECT_EQ(TranslateExpression("{(1,2);(3,4)}[1]"),
            "SELECT T2.A2 AS A1 FROM (SELECT CASE WHEN I0.i = 1 THEN T0.A1 WHEN I0.i = 2 THEN T1.A1 END AS A1, CASE "
            "WHEN I0.i = 1 THEN T0.A2 WHEN I0.i = 2 THEN T1.A2 END AS A2 FROM (SELECT 1, 2) AS T0, (SELECT 3, 4) AS "
            "T1, (VALUES (1), (2)) AS I0(i)) AS T2, (SELECT 1 AS A1) AS T3 WHERE T2.A1 = T3.A1");
}

// TODO: This test must take into account the special case when a full application is over a relational abstraction
TEST_F(TranslationTest, DISABLED_FullApplicationOnExpression1) {
  EXPECT_EQ(
      TranslateExpression("{B[1]}(x)"),
      "SELECT T2.A1 AS x FROM (SELECT T0.A2 AS A1 FROM B AS T0, (SELECT 1 AS A1) AS T1 WHERE T0.A1 = T1.A1) AS T2");
}

TEST_F(TranslationTest, AggregateExpression1) {
  EXPECT_EQ(TranslateExpression("sum[A]"), "SELECT SUM(T0.A1) AS A1 FROM A AS T0");
}

TEST_F(TranslationTest, AggregateExpression2) {
  EXPECT_EQ(TranslateExpression("average[A]"), "SELECT AVG(T0.A1) AS A1 FROM A AS T0");
}

TEST_F(TranslationTest, AggregateExpression3) {
  EXPECT_EQ(TranslateExpression("min[A]"), "SELECT MIN(T0.A1) AS A1 FROM A AS T0");
}

TEST_F(TranslationTest, AggregateExpression4) {
  EXPECT_EQ(TranslateExpression("max[A]"), "SELECT MAX(T0.A1) AS A1 FROM A AS T0");
}

TEST_F(TranslationTest, AggregateExpression5) {
  EXPECT_EQ(TranslateExpression("max[B[x]]"),
            "SELECT T1.x, MAX(T1.A1) AS A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0) AS T1 GROUP BY T1.x");
}

TEST_F(TranslationTest, AggregateExpression6) {
  EXPECT_EQ(TranslateExpression("sum[{(1,2);(3,4)}]"),
            "SELECT SUM(T2.A2) AS A1 FROM (SELECT CASE WHEN I0.i = 1 THEN T0.A1 WHEN I0.i = 2 THEN T1.A1 END AS A1, "
            "CASE WHEN I0.i = 1 THEN T0.A2 WHEN I0.i = 2 THEN T1.A2 END AS A2 FROM (SELECT 1, 2) AS T0, (SELECT 3, 4) "
            "AS T1, (VALUES (1), (2)) AS I0(i)) AS T2");
}

TEST_F(TranslationTest, AggregateExpression7) {
  EXPECT_EQ(TranslateExpression("max[{(1);(2);(3)}]"),
            "SELECT MAX(T3.A1) AS A1 FROM (SELECT CASE WHEN I0.i = 1 THEN T0.A1 WHEN I0.i = 2 THEN T1.A1 WHEN I0.i = 3 "
            "THEN T2.A1 END AS A1 FROM (SELECT 1) AS T0, (SELECT 2) AS T1, (SELECT 3) AS T2, (VALUES (1), (2), (3)) AS "
            "I0(i)) AS T3");
}

TEST_F(TranslationTest, RelationalAbstraction1) {
  EXPECT_EQ(
      TranslateExpression("{(1,2);(3,4)}"),
      "SELECT CASE WHEN I0.i = 1 THEN T0.A1 WHEN I0.i = 2 THEN T1.A1 END AS A1, CASE WHEN I0.i = 1 THEN T0.A2 WHEN "
      "I0.i = 2 THEN T1.A2 END AS A2 FROM (SELECT 1, 2) AS T0, (SELECT 3, 4) AS T1, (VALUES (1), (2)) AS I0(i)");
}

TEST_F(TranslationTest, RelationalAbstraction2) { EXPECT_EQ(TranslateExpression("{1}"), "SELECT 1 AS A1"); }

TEST_F(TranslationTest, RelationalAbstraction3) { EXPECT_EQ(TranslateExpression("{(1)}"), "SELECT 1"); }

TEST_F(TranslationTest, RelationalAbstraction4) { EXPECT_EQ(TranslateExpression("{(1,2)}"), "SELECT 1, 2"); }

TEST_F(TranslationTest, RelationalAbstraction5) {
  EXPECT_EQ(TranslateExpression("{1;2}"),
            "SELECT CASE WHEN I0.i = 1 THEN T0.A1 WHEN I0.i = 2 THEN T1.A1 END AS A1 FROM (SELECT 1 AS A1) AS T0, "
            "(SELECT 2 AS A1) AS T1, (VALUES (1), (2)) AS I0(i)");
}

TEST_F(TranslationTest, RelationalAbstraction6) {
  EXPECT_EQ(TranslateExpression("{(1);(2)}"),
            "SELECT CASE WHEN I0.i = 1 THEN T0.A1 WHEN I0.i = 2 THEN T1.A1 END AS A1 FROM (SELECT 1) AS T0, (SELECT 2) "
            "AS T1, (VALUES (1), (2)) AS I0(i)");
}

TEST_F(TranslationTest, FormulaBindings1) {
  EXPECT_EQ(TranslateExpression("(x): A(x)"),
            "WITH S0(x) AS (SELECT * FROM A AS T2) SELECT S0.x AS A1 FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1, S0 "
            "WHERE S0.x = T1.x");
}

TEST_F(TranslationTest, FormulaBindings2) {
  EXPECT_EQ(TranslateExpression("(x, x): A(x)"),
            "WITH S0(x) AS (SELECT * FROM A AS T2) SELECT S0.x AS A1, S0.x AS A2 FROM (SELECT T0.A1 AS x FROM A AS T0) "
            "AS T1, S0 "
            "WHERE S0.x = T1.x");
}

TEST_F(TranslationTest, FormulaBindings3) {
  EXPECT_EQ(TranslateExpression("(x): B(x,1)"),
            "WITH S0(x) AS (SELECT T3.A1 AS A1 FROM B AS T3) SELECT S0.x AS A1 FROM (SELECT T0.A1 AS x FROM B AS T0, "
            "(SELECT 1 AS A1) AS T1 WHERE T0.A2 = T1.A1) AS T2, S0 WHERE S0.x = T2.x");
}

TEST_F(TranslationTest, FormulaBindings4) {
  EXPECT_EQ(TranslateExpression("(x): {B[1]}(x)"),
            "WITH RA0 AS (SELECT T0.A2 AS A1 FROM B AS T0, (SELECT 1 AS A1) AS T1 WHERE T0.A1 = T1.A1), S0(x) AS "
            "(SELECT * FROM RA0) SELECT S0.x AS A1 FROM (SELECT RA0.A1 AS x FROM RA0) AS T3, S0 WHERE S0.x = T3.x");
}

TEST_F(TranslationTest, ExpressionBindings1) {
  EXPECT_EQ(TranslateExpression("[x]: A[x] where x > 1"),
            "WITH S0(x) AS (SELECT * FROM A AS T3) SELECT S0.x AS A1 FROM (SELECT T1.x FROM (SELECT T0.A1 AS x FROM A "
            "AS T0) AS T1 WHERE T1.x > 1) AS T2, S0 WHERE S0.x = T2.x");
}

TEST_F(TranslationTest, ExpressionBindings2) {
  EXPECT_EQ(TranslateExpression("[x in T, y in R]: F[x, y]"),
            "WITH S1(x) AS (SELECT * FROM T AS T3), S0(y) AS (SELECT * FROM R AS T2) SELECT S1.x AS A1, S0.y AS A2, "
            "T1.A1 AS A3 FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS A1 FROM F AS T0) AS T1, S1, S0 WHERE S1.x = "
            "T1.x AND S0.y = T1.y");
}

TEST_F(TranslationTest, ExpressionBindings3) {
  EXPECT_EQ(TranslateExpression("[x in A, y]: C[x, y] where D(y)"),
            "WITH S2(x) AS (SELECT * FROM A AS T7), S1(x, y) AS (SELECT T6.A1 AS A1, T6.A2 AS A2 FROM C AS T6), S0(y) "
            "AS (SELECT * FROM D AS T5) SELECT S2.x AS A1, S1.y AS A2, T4.A1 AS A3 FROM (SELECT T2.x, T2.y, T2.A1 FROM "
            "(SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS A1 FROM C AS T0) AS T2, (SELECT T1.A1 AS y FROM D AS T1) AS T3 "
            "WHERE T2.y = T3.y) AS T4, S2, S1, S0 WHERE S2.x = T4.x AND S1.x = T4.x AND S1.y = T4.y AND S0.y = T4.y "
            "AND S1.y = S0.y AND S2.x = S1.x");
}

TEST_F(TranslationTest, ExpressionBindings4) {
  EXPECT_EQ(TranslateExpression("[x in A, y in D]: C[x, y]"),
            "WITH S1(x) AS (SELECT * FROM A AS T3), S0(y) AS (SELECT * FROM D AS T2) SELECT S1.x AS A1, S0.y AS A2, "
            "T1.A1 AS A3 FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS A1 FROM C AS T0) AS T1, S1, S0 WHERE S1.x = "
            "T1.x AND S0.y = T1.y");
}

TEST_F(TranslationTest, ExpressionConstantTerms1) {
  EXPECT_EQ(TranslateExpression("B[1+2]"),
            "SELECT T0.A2 AS A1 FROM B AS T0, (SELECT 1 + 2 AS A1) AS T1 WHERE T0.A1 = T1.A1");
}

TEST_F(TranslationTest, ExpressionConstantTerms2) {
  EXPECT_EQ(TranslateExpression("B[2*(3+4)]"),
            "SELECT T0.A2 AS A1 FROM B AS T0, (SELECT 2 * (3 + 4) AS A1) AS T1 WHERE T0.A1 = T1.A1");
}

TEST_F(TranslationTest, DISABLED_ExpressionVariableTerms1) {
  EXPECT_EQ(TranslateExpression("[x] : x where A(x)"),
            "SELECT T1.x, T1.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0) AS T1 WHERE T1.x > 1 AND T1.x < 5");
}


TEST_F(TranslationTest, Program) {
  EXPECT_EQ(TranslateDefinition("def R {[x in A]: B[x]}"),
            "CREATE OR REPLACE VIEW R AS (WITH S0(x) AS (SELECT * FROM A AS T2) SELECT DISTINCT S0.x AS A1, T1.A1 AS "
            "A2 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0) AS T1, S0 WHERE S0.x = T1.x)");
}

TEST_F(TranslationTest, MultipleDefs1) {
  EXPECT_EQ(TranslateProgram("def R {(1, 2); (3, 4)} \n def S {(1, 4); (3, 4)}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2));\n\nCREATE OR "
            "REPLACE VIEW S AS (SELECT DISTINCT * FROM (VALUES (1, 4), (3, 4)) AS T1(A1, A2));");
}

TEST_F(TranslationTest, MultipleDefs2) {
  EXPECT_EQ(TranslateProgram("def R {(1, 2); (3, 4)} \n def S {B[1]} \n def T {B[3]}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2));\n\nCREATE OR "
            "REPLACE VIEW S AS (SELECT DISTINCT T1.A2 AS A1 FROM B AS T1, (SELECT 1 AS A1) AS T2 WHERE T1.A1 = "
            "T2.A1);\n\nCREATE OR REPLACE VIEW T AS (SELECT DISTINCT T4.A2 AS A1 FROM B AS T4, (SELECT 3 AS A1) AS T5 "
            "WHERE T4.A1 = T5.A1);");
}

TEST_F(TranslationTest, TableDefinition) {
  EXPECT_EQ(TranslateDefinition("def R {(1,2);(3,4)}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2))");
}

// Tests for EDB with named attributes
TEST_F(TranslationTest, NamedAttributesFormula) {
  default_edb_map["R"] = RelationInfo({"id", "name"});

  EXPECT_EQ(TranslateFormula("R(x, y)"), "SELECT T0.id AS x, T0.name AS y FROM R AS T0");
}

TEST_F(TranslationTest, NamedAttributesConjunction) {
  default_edb_map["R"] = RelationInfo({"id", "name"});
  default_edb_map["S"] = RelationInfo({"student_id", "grade"});

  EXPECT_EQ(TranslateFormula("R(x, y) and S(x, z)"),
            "SELECT T1.x, T1.y, T3.z FROM (SELECT T0.id AS x, T0.name AS y FROM R AS T0) AS T1, (SELECT T2.student_id "
            "AS x, T2.grade AS z FROM S AS T2) AS T3 WHERE T1.x = T3.x");
}

TEST_F(TranslationTest, NamedAttributesExistential) {
  default_edb_map["R"] = RelationInfo({"student_id", "course_id"});
  default_edb_map["S"] = RelationInfo({"course_id"});

  EXPECT_EQ(TranslateFormula("exists ((y in S) | R(x, y))"),
            "SELECT T2.x FROM (SELECT T1.student_id AS x, T1.course_id AS y FROM R AS T1) AS T2, S AS T0 WHERE T2.y = "
            "T0.course_id");
}

TEST_F(TranslationTest, NamedAttributesPartialApplication) {
  default_edb_map["R"] = RelationInfo({"student_id", "course_id", "grade"});

  EXPECT_EQ(
      TranslateDefinition("def S {R[x]}"),
      "CREATE OR REPLACE VIEW S AS (SELECT DISTINCT T0.student_id AS x, T0.course_id AS A1, T0.grade AS A2 FROM R "
      "AS T0)");
}

TEST_F(TranslationTest, NamedAttributesAggregate) {
  default_edb_map["R"] = RelationInfo({"student_id", "grade"});

  EXPECT_EQ(TranslateDefinition("def S {max[R[x]]}"),
            "CREATE OR REPLACE VIEW S AS (SELECT DISTINCT T1.x, MAX(T1.A1) AS A1 FROM (SELECT T0.student_id AS x, "
            "T0.grade AS A1 FROM R AS T0) AS T1 GROUP BY T1.x)");
}

// Tests for EDB with single attribute
TEST_F(TranslationTest, SingleNamedAttribute) {
  default_edb_map["R"] = RelationInfo({"id"});

  EXPECT_EQ(TranslateFormula("R(x)"), "SELECT T0.id AS x FROM R AS T0");
}

// Tests for EDB with three attributes
TEST_F(TranslationTest, ThreeNamedAttributes) {
  default_edb_map["R"] = RelationInfo({"student_id", "course_id", "grade"});

  EXPECT_EQ(TranslateFormula("R(x, y, z)"), "SELECT T0.student_id AS x, T0.course_id AS y, T0.grade AS z FROM R AS T0");
}

// Test for EDB with repeated variables using named attributes
TEST_F(TranslationTest, NamedAttributesRepeatedVariables) {
  default_edb_map["R"] = RelationInfo({"id", "parent_id"});

  EXPECT_EQ(TranslateFormula("R(x, x)"), "SELECT T0.id AS x FROM R AS T0 WHERE T0.id = T0.parent_id");
}

TEST_F(TranslationTest, NamedAttributesBindingFormula) {
  default_edb_map["F"] = RelationInfo({"name"});

  EXPECT_EQ(TranslateExpression("(x): F(x)"),
            "WITH S0(x) AS (SELECT * FROM F AS T2) SELECT S0.x AS A1 FROM (SELECT T0.name AS x FROM F AS T0) AS T1, S0 "
            "WHERE S0.x = T1.x");
}

TEST_F(TranslationTest, CompositionRelation) {
  EXPECT_EQ(TranslateExpression("(x, y): exists((z) | B(x, z) and E(z, y))"),
            "WITH S1(y) AS (SELECT T7.A2 AS A2 FROM E AS T7), S0(x) AS (SELECT T6.A1 AS A1 FROM B AS T6) SELECT S0.x "
            "AS A1, S1.y AS A2 FROM (SELECT T4.x, T4.y FROM (SELECT T1.x, T1.z, T3.y FROM (SELECT T0.A1 AS x, T0.A2 AS "
            "z FROM B AS T0) AS T1, (SELECT T2.A1 AS z, T2.A2 AS y FROM E AS T2) AS T3 WHERE T1.z = T3.z) AS T4) AS "
            "T5, S1, S0 WHERE S1.y = T5.y AND S0.x = T5.x");
}

TEST_F(TranslationTest, SelfComposition) {
  EXPECT_EQ(TranslateExpression("(x, y): exists((z) | B(x, z) and B(z, y))"),
            "WITH S0(x, y) AS (SELECT * FROM B AS T6) SELECT S0.x AS A1, S0.y AS A2 FROM (SELECT T4.x, T4.y FROM "
            "(SELECT T1.x, T1.z, T3.y FROM (SELECT T0.A1 AS x, T0.A2 AS z FROM B AS T0) AS T1, (SELECT T2.A1 AS z, "
            "T2.A2 AS y FROM B AS T2) AS T3 WHERE T1.z = T3.z) AS T4) AS T5, S0 WHERE S0.x = T5.x AND S0.y = T5.y");
}

TEST_F(TranslationTest, FirstTransitivityComposition) {
  EXPECT_EQ(
      TranslateExpression("(x, y): B(x, y) or exists((z) | B(x, z) and B(z, y))"),
      "WITH S0(x, y) AS (SELECT * FROM B AS T9) SELECT S0.x AS A1, S0.y AS A2 FROM (SELECT T6.x, T6.y FROM (SELECT "
      "T0.A1 AS x, T0.A2 AS y FROM B AS T0) AS T6 UNION SELECT T7.x, T7.y FROM (SELECT T5.x, T5.y FROM (SELECT T2.x, "
      "T2.z, T4.y FROM (SELECT T1.A1 AS x, T1.A2 AS z FROM B AS T1) AS T2, (SELECT T3.A1 AS z, T3.A2 AS y FROM B AS "
      "T3) AS T4 WHERE T2.z = T4.z) AS T5) AS T7) AS T8, S0 WHERE S0.x = T8.x AND S0.y = T8.y");
}

TEST_F(TranslationTest, SimpleReferenceDefinition) {
  EXPECT_EQ(TranslateDefinition("def R {A}"), "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT T0.A1 AS A1 FROM A AS T0)");
}

TEST_F(TranslationTest, ExistentialNotBoundingAllVariables) {
  EXPECT_EQ(TranslateFormula("exists((y) | A(x) and B(y))"),
            "SELECT T4.x FROM (SELECT T1.x, T3.y FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1, (SELECT T2.A1 AS y FROM "
            "B AS T2) AS T3) AS T4");
}

TEST_F(TranslationTest, RecursiveDefinition) {
  default_edb_map["A"] = RelationInfo(1);
  default_edb_map["B"] = RelationInfo(1);
  default_edb_map["C"] = RelationInfo(1);

  EXPECT_EQ(TranslateDefinition("def Q {(x in A) : B(x) or exists ((y) | Q(y) and C(y))}"),
            "CREATE OR REPLACE VIEW Q AS (WITH RECURSIVE S0(x) AS (SELECT * FROM A AS T9), R0(A1) AS (SELECT T6.x FROM "
            "(SELECT T0.A1 AS x FROM B AS T0) AS T6 UNION SELECT  FROM (SELECT  FROM (SELECT T2.y FROM (SELECT T1.A1 "
            "AS y FROM R0 AS T1) AS T2, (SELECT T3.A1 AS y FROM C AS T3) AS T4 WHERE T2.y = T4.y) AS T5) AS T7) SELECT "
            "DISTINCT S0.x AS A1 FROM (SELECT R0.A1 AS x FROM R0) AS T8, S0 WHERE S0.x = T8.x)");
}

TEST_F(TranslationTest, TransitiveClosure) {
  default_edb_map["R"] = RelationInfo(2);

  EXPECT_EQ(TranslateDefinition("def Q {(x,y) : R(x,y) or exists((z) | R(x,z) and Q(z,y))}"),
            "CREATE OR REPLACE VIEW Q AS (WITH RECURSIVE S1(y) AS (SELECT T10.A2 AS A2 FROM R AS T10), S0(x) AS "
            "(SELECT T9.A1 AS A1 FROM R AS T9), R0(A1, A2) AS (SELECT T6.x, T6.y FROM (SELECT T0.A1 AS x, T0.A2 AS y "
            "FROM R AS T0) AS T6 UNION SELECT T7.x, T7.y FROM (SELECT T5.x, T5.y FROM (SELECT T2.x, T2.z, T4.y FROM "
            "(SELECT T1.A1 AS x, T1.A2 AS z FROM R AS T1) AS T2, (SELECT T3.A1 AS z, T3.A2 AS y FROM R0 AS T3) AS T4 "
            "WHERE T2.z = T4.z) AS T5) AS T7) SELECT DISTINCT S0.x AS A1, S1.y AS A2 FROM (SELECT R0.A1 AS x, R0.A2 AS "
            "y FROM R0) AS T8, S1, S0 WHERE S1.y = T8.y AND S0.x = T8.x)");
}

TEST_F(TranslationTest, WeirdEdgeCase1) {
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

  // Extract the substring matching CREATE OR REPLACE VIEW jrs.*; from both outputs
  std::regex pattern(R"(CREATE OR REPLACE VIEW jrs.*?;)");
  std::smatch match1, match2;

  bool found1 = std::regex_search(output1, match1, pattern);
  bool found2 = std::regex_search(output2, match2, pattern);

  ASSERT_TRUE(found1) << "Pattern not found in output1";
  ASSERT_TRUE(found2) << "Pattern not found in output2";

  EXPECT_EQ(match1.str(), match2.str());

  auto parser1 = GetParser(input1);
  auto tree1 = parser1->program();
  Preprocessor preprocessor1(default_edb_map);
  auto ast1 = preprocessor1.Process(tree1);

  auto parser2 = GetParser(input2);
  auto tree2 = parser2->program();
  Preprocessor preprocessor2(default_edb_map);
  auto ast2 = preprocessor2.Process(tree2);

  // Find the jrs RelDefContext in both ASTs
  psr::RelDefContext* jrs_ctx1 = nullptr;
  psr::RelDefContext* jrs_ctx2 = nullptr;

  for (auto& rel_def : tree1->relDef()) {
    if (rel_def->T_ID()->getText() == "jrs") {
      jrs_ctx1 = rel_def;
      break;
    }
  }

  for (auto& rel_def : tree2->relDef()) {
    if (rel_def->T_ID()->getText() == "jrs") {
      jrs_ctx2 = rel_def;
      break;
    }
  }

  ASSERT_NE(jrs_ctx1, nullptr) << "jrs definition not found in input1";
  ASSERT_NE(jrs_ctx2, nullptr) << "jrs definition not found in input2";

  // Compare ExtendedNode for the jrs definitions using operator==
  const auto node1 = ast1.GetNode(jrs_ctx1);
  const auto node2 = ast2.GetNode(jrs_ctx2);

  ASSERT_NE(node1, nullptr) << "jrs node1 is null";
  ASSERT_NE(node2, nullptr) << "jrs node2 is null";

  // try {
  //   EXPECT_TRUE(*node1 == *node2) << "ExtendedNodes for jrs definition differ";
  // } catch (const ExtendedNodeDifferenceException& e) {
  //   FAIL() << "ExtendedNode difference found: " << e.what() << " (field: " << e.GetFieldName()
  //          << ", details: " << e.GetDetails() << ")";
  // }
}

// TODO: This test fails because we don't have a way to translate a comparison formula that is an equality
TEST_F(TranslationTest, DISABLED_BindingEquality) {
  EXPECT_EQ(TranslateExpression("(x): x = 1"), "SELECT T0.name AS x FROM F AS T0");
}

}  // namespace rel2sql
