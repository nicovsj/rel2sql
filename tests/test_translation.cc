// cspell:ignore GTEST
#include <gtest/gtest.h>

#include <regex>

#include "api/translate.h"
#include "rel_ast/domain.h"
#include "rel_ast/rel_ast.h"
#include "rel_ast/relation_info.h"
#include "sql/translator.h"
#include "support/exceptions.h"
#include "test_common.h"

namespace rel2sql {

namespace {

RelContext BuildContextFromFormula(const std::string& formula, const RelationMap& edb_map) {
  auto parser = GetParser(formula);
  auto tree = parser->formula();
  RelASTBuilder ast_builder;
  auto root = ast_builder.BuildFromFormula(tree);
  RelContextBuilder builder(edb_map);
  return builder.Process(root);
}

}  // namespace

class TranslationTest : public ::testing::Test {
 protected:
  void SetUp() override { default_edb_map = CreateDefaultEDBMap(); }

  // Translate a Domain to SQL string. Domains are independent of the input formula;
  // a minimal context (A(x) with default EDB) is used internally. Applies optimizer.
  std::string DomainToSqlString(const Domain& domain) {
    auto context = BuildContextFromFormula("A(x)", CreateDefaultEDBMap());
    Translator translator(context);
    auto sql = translator.DomainToSql(domain);
    if (!sql) return "";
    auto expr = std::static_pointer_cast<sql::ast::Expression>(sql);
    sql::ast::Optimizer optimizer;
    expr = optimizer.Optimize(expr);
    return expr ? expr->ToString() : "";
  }

  std::string TranslateFormula(const std::string& input) {
    auto sql = GetSQLFromFormula(input, default_edb_map);
    return sql ? sql->ToString() : "";
  }

  std::string TranslateExpression(const std::string& input) {
    auto sql = GetSQLFromExpr(input, default_edb_map);
    return sql ? sql->ToString() : "";
  }

  std::string TranslateProgram(const std::string& input) {
    auto sql = GetUnoptimizedSQLRel(input, default_edb_map);
    return sql ? sql->ToString() : "";
  }

  std::string TranslateDefinition(const std::string& input) {
    auto sql = GetUnoptimizedSQLRel(input, default_edb_map);
    return sql ? sql->ToString() : "";
  }

  rel2sql::RelationMap default_edb_map;
};

TEST_F(TranslationTest, FullApplication1) { EXPECT_EQ(TranslateFormula("A(x)"), "SELECT T0.A1 AS x FROM A AS T0"); }

TEST_F(TranslationTest, FullApplication2) {
  EXPECT_EQ(TranslateFormula("B(x,y)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0");
}

TEST_F(TranslationTest, FullApplication3) {
  EXPECT_EQ(TranslateFormula("C(x,y,z)"), "SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0");
}

TEST_F(TranslationTest, FullApplication4) {
  EXPECT_EQ(TranslateFormula("B(x,x)"), "SELECT T0.A1 AS x FROM B AS T0 WHERE T0.A1 = T0.A2");
}

TEST_F(TranslationTest, FullApplication5) {
  EXPECT_EQ(TranslateFormula("C(x,x,x)"), "SELECT T0.A1 AS x FROM C AS T0 WHERE T0.A1 = T0.A2 AND T0.A2 = T0.A3");
}

TEST_F(TranslationTest, FullApplication6) {
  EXPECT_EQ(TranslateFormula("C(x,y,x)"), "SELECT T0.A1 AS x, T0.A2 AS y FROM C AS T0 WHERE T0.A1 = T0.A3");
}

TEST_F(TranslationTest, FullApplication7) {
  EXPECT_EQ(TranslateFormula("{1}(x)"), "SELECT T1.A1 AS x FROM (SELECT 1 AS A1) AS T1");
}

TEST_F(TranslationTest, FullApplication8) {
  EXPECT_EQ(TranslateFormula("B(A,x)"),
            "SELECT T0.A2 AS x FROM B AS T0, (SELECT T1.A1 AS A1 FROM A AS T1) AS T2 WHERE T0.A1 = T2.A1");
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
            "SELECT T1.x, T1.y, T1.z FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0) AS T1 WHERE (T1.x + "
            "T1.y) * T1.z > 100");
}

TEST_F(TranslationTest, ArithmeticInComparisons6) {
  EXPECT_EQ(TranslateFormula("C(x,y,z) and x + y + z = 15"),
            "SELECT T1.x, T1.y, T1.z FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0) AS T1 WHERE T1.x + "
            "T1.y + T1.z = 15");
}

TEST_F(TranslationTest, InferrableVariableConjunction1) {
  EXPECT_EQ(TranslateExpression("x = y + 1 and A(y)"),
            "SELECT T1.y, T1.y + 1 AS x FROM (SELECT T0.A1 AS y FROM A AS T0) AS T1");
}

TEST_F(TranslationTest, InferrableVariableConjunction2) {
  EXPECT_EQ(TranslateExpression("z = x + 1 and A(x)"),
            "SELECT T1.x, T1.x + 1 AS z FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1");
}

TEST_F(TranslationTest, InferrableVariableConjunction3) {
  EXPECT_EQ(TranslateExpression("x = 2 * y - 3 and A(y)"),
            "SELECT T1.y, 2 * T1.y - 3 AS x FROM (SELECT T0.A1 AS y FROM A AS T0) AS T1");
}

TEST_F(TranslationTest, InferrableVariableMultivariate) {
  EXPECT_EQ(TranslateExpression("z = x + y + 1 and B(x, y)"),
            "SELECT T1.x, T1.y, T1.x + T1.y + 1 AS z FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0) AS T1");
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
            "SELECT T2.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T2, (SELECT T1.A1 AS x FROM D AS T1) AS T3 WHERE "
            "T2.x = T3.x");
}

TEST_F(TranslationTest, DisjunctionFormula) {
  EXPECT_EQ(TranslateFormula("A(x) or D(x)"), "SELECT T0.A1 AS x FROM A AS T0 UNION SELECT T1.A1 AS x FROM D AS T1");
}

TEST_F(TranslationTest, DisjunctionFormula2) {
  EXPECT_EQ(TranslateFormula("A(x) or (D(x) and G(x))"),
            "SELECT T0.A1 AS x FROM A AS T0 UNION SELECT T3.x FROM (SELECT T1.A1 AS x FROM D AS T1) AS T3, (SELECT "
            "T2.A1 AS x FROM G AS T2) AS T4 WHERE T3.x = T4.x");
}

TEST_F(TranslationTest, DisjunctionFormula3) {
  EXPECT_EQ(TranslateFormula("A(x) or D(y)"),
            "SELECT T0.A1 AS x FROM A AS T0 UNION SELECT T3.x FROM (SELECT T1.A1 AS x FROM D AS T1) AS T3, (SELECT "
            "T2.A1 AS x FROM G AS T2) AS T4 WHERE T3.x = T4.x");
}

TEST_F(TranslationTest, NegationFormula1) {
  // Negation uses WITH + NOT IN per Rel2SQL paper. Conjunction joins lhs (A) with negation result.
  std::string result = TranslateFormula("A(x) and not D(x)");
  EXPECT_TRUE(result.find("NOT IN") != std::string::npos);
  EXPECT_TRUE(result.find("FROM A") != std::string::npos);
  EXPECT_TRUE(result.find("FROM D") != std::string::npos);
}

TEST_F(TranslationTest, NegationFormula2) {
  std::string result = TranslateFormula("not A(x) and D(x)");
  EXPECT_TRUE(result.find("NOT IN") != std::string::npos);
  EXPECT_TRUE(result.find("FROM A") != std::string::npos);
  EXPECT_TRUE(result.find("FROM D") != std::string::npos);
}

TEST_F(TranslationTest, NegationFormula3) {
  std::string result = TranslateFormula("A(x) and not (D(x) or G(x))");
  EXPECT_TRUE(result.find("NOT IN") != std::string::npos);
  EXPECT_TRUE(result.find("UNION") != std::string::npos);
}

TEST_F(TranslationTest, NegationFormula4) {
  std::string result = TranslateFormula("A(x) and D(y) and not D(x) and not A(y)");
  EXPECT_TRUE(result.find("NOT IN") != std::string::npos);
  EXPECT_TRUE(result.find("FROM A") != std::string::npos);
  EXPECT_TRUE(result.find("FROM D") != std::string::npos);
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
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0) AS T1, A AS T2 WHERE T1.y = T2.A1");
}

TEST_F(TranslationTest, ExistentialFormula4) {
  EXPECT_EQ(TranslateFormula("exists ((y in A, z in D) | C(x, y, z))"),
            "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0) AS T1, A AS T2, D AS T3 WHERE "
            "T1.y = T2.A1 AND T1.z = T3.A1");
}

TEST_F(TranslationTest, ExistentialFormula5) {
  EXPECT_EQ(
      TranslateFormula("exists ((y in A, z) | C(x, y, z))"),
      "SELECT T1.x FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0) AS T1, A AS T2 WHERE T1.y = T2.A1");
}

TEST_F(TranslationTest, NestedQuantifiers1) {
  EXPECT_EQ(
      TranslateFormula("exists ((y) | exists ((z) | C(x, y, z)))"),
      "SELECT T2.x FROM (SELECT T1.x, T1.y FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM C AS T0) AS T1) AS "
      "T2");
}

TEST_F(TranslationTest, NestedQuantifiers2) {
  EXPECT_EQ(TranslateFormula("exists ((y in A) | forall ((z in D) | C(x, y, z)))"),
            "SELECT T3.x FROM (SELECT T2.x, T2.y FROM (SELECT T1.A1 AS x, T1.A2 AS y, T1.A3 AS z FROM C AS T1) AS T2 "
            "WHERE NOT EXISTS (SELECT * FROM D AS T0 WHERE (T2.x, T2.y, T0.A1) NOT IN (SELECT * FROM (SELECT T1.A1 AS "
            "x, T1.A2 AS y, T1.A3 AS z FROM C AS T1) AS T2))) AS T3, A AS T4 WHERE T3.y = T4.A1");
}

TEST_F(TranslationTest, NestedQuantifiers3) {
  EXPECT_EQ(TranslateFormula("forall ((y in A) | exists ((z) | C(x, y, z)))"),
            "SELECT T3.x FROM (SELECT T2.x, T2.y FROM (SELECT T1.A1 AS x, T1.A2 AS y, T1.A3 AS z FROM C AS T1) AS T2) "
            "AS T3 WHERE NOT EXISTS (SELECT * FROM A AS T0 WHERE (T3.x, T0.A1) NOT IN (SELECT * FROM (SELECT T2.x, "
            "T2.y FROM (SELECT T1.A1 AS x, T1.A2 AS y, T1.A3 AS z FROM C AS T1) AS T2) AS T3))");
}

TEST_F(TranslationTest, NestedQuantifiers4) {
  EXPECT_EQ(TranslateFormula("exists ((y) | exists ((z) | exists ((w) | I(x, y, z) and I(y, z, w))))"),
            "SELECT T6.x FROM (SELECT T5.x, T5.y FROM (SELECT T4.x, T4.y, T4.z FROM (SELECT T2.x, T2.y, T2.z, T3.w "
            "FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS z FROM I AS T0) AS T2, (SELECT T1.A1 AS y, T1.A2 AS z, "
            "T1.A3 AS w FROM I AS T1) AS T3 WHERE T2.z = T3.z AND T2.y = T3.y) AS T4) AS T5) AS T6");
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
      "SELECT T5.x, T5.A1 FROM (SELECT T0.A1 AS x, T0.A2 AS A1 FROM B AS T0) AS T5, (SELECT T3.x FROM (SELECT T1.A1 AS "
      "x FROM A AS T1) AS T3, (SELECT T2.A1 AS x FROM D AS T2) AS T4 WHERE T3.x = T4.x) AS T6 WHERE T5.x = T6.x");
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
            "SELECT T2.x, T0.A3 AS A1 FROM C AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM B AS T1) AS T2 WHERE T0.A1 = "
            "T2.A1 AND T0.A2 = T2.x");
}

TEST_F(TranslationTest, PartialApplicationSharingVariables4) {
  EXPECT_EQ(TranslateExpression("C[B[x], x, y]"),
            "SELECT T2.x, T0.A3 AS y FROM C AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM B AS T1) AS T2 WHERE T0.A1 = "
            "T2.A1 AND T0.A2 = T2.x");
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
  EXPECT_EQ(TranslateExpression("(x): A(x)"), "SELECT T1.x AS A1 FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1");
}

TEST_F(TranslationTest, FormulaBindings2) {
  EXPECT_EQ(TranslateExpression("(x, x): A(x)"),
            "SELECT T1.x AS A1, T1.x AS A2 FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1");
}

TEST_F(TranslationTest, FormulaBindings3) {
  EXPECT_EQ(
      TranslateExpression("(x): B(x,1)"),
      "SELECT T2.x AS A1 FROM (SELECT T0.A1 AS x FROM B AS T0, (SELECT 1 AS A1) AS T1 WHERE T0.A2 = T1.A1) AS T2");
}

TEST_F(TranslationTest, FormulaBindings4) {
  EXPECT_EQ(TranslateExpression("(x): {B[1]}(x)"),
            "SELECT T4.x AS A1 FROM (SELECT T3.A1 AS x FROM (SELECT T0.A2 AS A1 FROM B AS T0, (SELECT 1 AS A1) AS T1 "
            "WHERE T0.A1 = T1.A1) AS T3) AS T4");
}

TEST_F(TranslationTest, FormulaBindings5) {
  EXPECT_EQ(TranslateExpression("(x): A(x) and D(x)"),
            "SELECT T4.x AS A1 FROM (SELECT T2.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T2, (SELECT T1.A1 AS x FROM "
            "D AS T1) AS T3 WHERE T2.x = T3.x) AS T4");
}

TEST_F(TranslationTest, FormulaBindings6) {
  EXPECT_EQ(TranslateExpression("(x in A): D(x)"),
            "SELECT T4.x AS A1 FROM (SELECT T2.x FROM (SELECT T0.A1 AS x FROM D AS T0) AS T2, (SELECT T1.A1 AS x FROM "
            "A AS T1) AS T3 WHERE T2.x = T3.x) AS T4");
}

TEST_F(TranslationTest, ExpressionBindings1) {
  EXPECT_EQ(TranslateExpression("[x]: A[x] where x > 1"),
            "SELECT T2.x AS A1 FROM (SELECT T1.x FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1 WHERE T1.x > 1) AS T2");
}

TEST_F(TranslationTest, ExpressionBindings2) {
  EXPECT_EQ(TranslateExpression("[x in A, y in D]: C[x, y]"),
            "SELECT T7.x AS A1, T7.y AS A2, T7.A1 AS A3 FROM (SELECT T5.x, T5.y, T5.A1 FROM (SELECT T0.A1 AS x, T0.A2 "
            "AS y, T0.A3 AS A1 FROM C AS T0) AS T5, (SELECT T3.x, T4.y FROM (SELECT T1.A1 AS x FROM A AS T1) AS T3, "
            "(SELECT T2.A1 AS y FROM D AS T2) AS T4) AS T6 WHERE T5.y = T6.y AND T5.x = T6.x) AS T7");
}

TEST_F(TranslationTest, ExpressionBindings3) {
  EXPECT_EQ(TranslateExpression("[x in A, y]: C[x, y] where D(y)"),
            "SELECT T7.x AS A1, T7.y AS A2, T7.A1 AS A3 FROM (SELECT T5.x, T5.y, T5.A1 FROM (SELECT T2.x, T2.y, T2.A1 "
            "FROM (SELECT T0.A1 AS x, T0.A2 AS y, T0.A3 AS A1 FROM C AS T0) AS T2, (SELECT T1.A1 AS y FROM D AS T1) AS "
            "T3 WHERE T2.y = T3.y) AS T5, (SELECT T4.A1 AS x FROM A AS T4) AS T6 WHERE T5.x = T6.x) AS T7");
}

TEST_F(TranslationTest, ExpressionBindings4) {
  EXPECT_EQ(TranslateExpression("[x in A, y in D]: C[x, y]"),
            "SELECT T7.x AS A1, T7.y AS A2, T7.A1 AS A3 FROM (SELECT T5.x, T5.y, T5.A1 FROM (SELECT T0.A1 AS x, T0.A2 "
            "AS y, T0.A3 AS A1 FROM C AS T0) AS T5, (SELECT T3.x, T4.y FROM (SELECT T1.A1 AS x FROM A AS T1) AS T3, "
            "(SELECT T2.A1 AS y FROM D AS T2) AS T4) AS T6 WHERE T5.y = T6.y AND T5.x = T6.x) AS T7");
}

TEST_F(TranslationTest, ExpressionConstantTerms1) {
  EXPECT_EQ(TranslateExpression("B[1+2]"),
            "SELECT T0.A2 AS A1 FROM B AS T0, (SELECT 1 + 2 AS A1) AS T1 WHERE T0.A1 = T1.A1");
}

TEST_F(TranslationTest, ExpressionConstantTerms2) {
  EXPECT_EQ(TranslateExpression("B[2*(3+4)]"),
            "SELECT T0.A2 AS A1 FROM B AS T0, (SELECT 2 * (3 + 4) AS A1) AS T1 WHERE T0.A1 = T1.A1");
}

TEST_F(TranslationTest, ParameterVariableTerms1) {
  EXPECT_EQ(TranslateExpression("A(x+1)"), "SELECT T0.A1 - 1 AS x FROM A AS T0");
}

TEST_F(TranslationTest, ParameterVariableTerms2) {
  EXPECT_EQ(TranslateExpression("A(2*x)"), "SELECT T0.A1 / 2 AS x FROM A AS T0");
}

TEST_F(TranslationTest, ParameterVariableTerms3) {
  EXPECT_EQ(TranslateExpression("A(2*x-1)"), "SELECT (T0.A1 + 1) / 2 AS x FROM A AS T0");
}

TEST_F(TranslationTest, ParameterVariableTerms4) {
  EXPECT_EQ(TranslateExpression("A(3*(2*x-1+5*x)+x)"), "SELECT (T0.A1 + 3) / 22 AS x FROM A AS T0");
}

TEST_F(TranslationTest, ParameterVariableTerms5) {
  EXPECT_EQ(TranslateExpression("B(x+1,x-1)"), "SELECT T0.A1 - 1 AS x FROM B AS T0 WHERE T0.A1 - 1 = T0.A2 + 1");
}

TEST_F(TranslationTest, ParameterVariableTerms6) {
  EXPECT_EQ(TranslateExpression("B(x+1,x,y)"),
            "SELECT T0.A1 - 1 AS x, T0.A3 AS y FROM B AS T0 WHERE T0.A1 - 1 = T0.A2");
}

TEST_F(TranslationTest, ParameterVariableTerms7) {
  EXPECT_EQ(TranslateExpression("B(x+1,B[x])"),
            "SELECT T2.x FROM B AS T0, (SELECT T1.A1 AS x, T1.A2 AS A1 FROM B AS T1) AS T2 WHERE T0.A2 = T2.A1 AND "
            "T0.A1 - 1 = T2.x");
}

TEST_F(TranslationTest, FailedParameterVariableTerms1) {
  EXPECT_THROW(TranslateExpression("A(x+y)"), VariableException);
}

TEST_F(TranslationTest, FailedParameterVariableTerms2) {
  EXPECT_THROW(TranslateExpression("A(x*x)"), VariableException);
}

TEST_F(TranslationTest, FailedParameterVariableTerms3) {
  EXPECT_THROW(TranslateExpression("A(x*x - x*x)"), VariableException);
}

TEST_F(TranslationTest, FailedParameterVariableTerms4) {
  EXPECT_THROW(TranslateExpression("A((1-1)*x)"), VariableException);
}

TEST_F(TranslationTest, ExpressionAsTermRewriterBindingsBody) {
  // [x in A, y in A]: x+y+1  ->  [x,y]: { (z): z = x+y+1 and A(x) and A(y) }
  EXPECT_EQ(TranslateExpression("[x in A, y in A]: x+y+1"),
            "SELECT T6.x AS A1, T6.y AS A2, T6.A1 AS A3 FROM (SELECT T4.x, T4.y, T4._x0 AS A1 FROM (SELECT T1.x, T3.y, "
            "T1.x + T3.y + 1 AS _x0 FROM (SELECT T0.A1 AS x FROM A AS T0) AS T1, (SELECT T2.A1 AS y FROM A AS T2) AS "
            "T3) AS T4) AS T6");
}

TEST_F(TranslationTest, Program) {
  EXPECT_EQ(
      TranslateDefinition("def R {[x in A]: B[x]}"),
      "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT T4.x AS A1, T4.A1 AS A2 FROM (SELECT T2.x, T2.A1 FROM (SELECT "
      "T0.A1 AS x, T0.A2 AS A1 FROM B AS T0) AS T2, (SELECT T1.A1 AS x FROM A AS T1) AS T3 WHERE T2.x = T3.x) AS T4);");
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
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0(A1, A2));");
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
            "SELECT T2.x, T2.y, T3.z FROM (SELECT T0.id AS x, T0.name AS y FROM R AS T0) AS T2, (SELECT T1.student_id "
            "AS x, T1.grade AS z FROM S AS T1) AS T3 WHERE T2.x = T3.x");
}

TEST_F(TranslationTest, NamedAttributesExistential) {
  default_edb_map["R"] = RelationInfo({"student_id", "course_id"});
  default_edb_map["S"] = RelationInfo({"course_id"});

  EXPECT_EQ(TranslateFormula("exists ((y in S) | R(x, y))"),
            "SELECT T1.x FROM (SELECT T0.student_id AS x, T0.course_id AS y FROM R AS T0) AS T1, S AS T2 WHERE T1.y = "
            "T2.course_id");
}

TEST_F(TranslationTest, NamedAttributesPartialApplication) {
  default_edb_map["R"] = RelationInfo({"student_id", "course_id", "grade"});

  EXPECT_EQ(
      TranslateDefinition("def S {R[x]}"),
      "CREATE OR REPLACE VIEW S AS (SELECT DISTINCT T0.student_id AS x, T0.course_id AS A1, T0.grade AS A2 FROM R "
      "AS T0);");
}

TEST_F(TranslationTest, NamedAttributesAggregate) {
  default_edb_map["R"] = RelationInfo({"student_id", "grade"});

  EXPECT_EQ(TranslateDefinition("def S {max[R[x]]}"),
            "CREATE OR REPLACE VIEW S AS (SELECT DISTINCT T1.x, MAX(T1.A1) AS A1 FROM (SELECT T0.student_id AS x, "
            "T0.grade AS A1 FROM R AS T0) AS T1 GROUP BY T1.x);");
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

  EXPECT_EQ(TranslateExpression("(x): F(x)"), "SELECT T1.x AS A1 FROM (SELECT T0.name AS x FROM F AS T0) AS T1");
}

TEST_F(TranslationTest, CompositionRelation) {
  EXPECT_EQ(TranslateExpression("(x, y): exists((z) | B(x, z) and E(z, y))"),
            "SELECT T5.x AS A1, T5.y AS A2 FROM (SELECT T4.x, T4.y FROM (SELECT T2.x, T2.z, T3.y FROM (SELECT T0.A1 AS "
            "x, T0.A2 AS z FROM B AS T0) AS T2, (SELECT T1.A1 AS z, T1.A2 AS y FROM E AS T1) AS T3 WHERE T2.z = T3.z) "
            "AS T4) AS T5");
}

TEST_F(TranslationTest, SelfComposition) {
  EXPECT_EQ(TranslateExpression("(x, y): exists((z) | B(x, z) and B(z, y))"),
            "SELECT T5.x AS A1, T5.y AS A2 FROM (SELECT T4.x, T4.y FROM (SELECT T2.x, T2.z, T3.y FROM (SELECT T0.A1 AS "
            "x, T0.A2 AS z FROM B AS T0) AS T2, (SELECT T1.A1 AS z, T1.A2 AS y FROM B AS T1) AS T3 WHERE T2.z = T3.z) "
            "AS T4) AS T5");
}

TEST_F(TranslationTest, FirstTransitivityComposition) {
  EXPECT_EQ(TranslateExpression("(x, y): B(x, y) or exists((z) | B(x, z) and B(z, y))"),
            "SELECT T6.x AS A1, T6.y AS A2 FROM (SELECT T0.A1 AS x, T0.A2 AS y FROM B AS T0 UNION SELECT T5.x, T5.y "
            "FROM (SELECT T3.x, T3.z, T4.y FROM (SELECT T1.A1 AS x, T1.A2 AS z FROM B AS T1) AS T3, (SELECT T2.A1 AS "
            "z, T2.A2 AS y FROM B AS T2) AS T4 WHERE T3.z = T4.z) AS T5) AS T6");
}

TEST_F(TranslationTest, SimpleReferenceDefinition) {
  EXPECT_EQ(TranslateDefinition("def R {A}"),
            "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT T0.A1 AS A1 FROM A AS T0);");
}

TEST_F(TranslationTest, ExistentialNotBoundingAllVariables) {
  EXPECT_EQ(TranslateFormula("exists((y) | A(x) and D(y))"),
            "SELECT T4.x FROM (SELECT T2.x, T3.y FROM (SELECT T0.A1 AS x FROM A AS T0) AS T2, (SELECT T1.A1 AS y FROM "
            "D AS T1) AS T3) AS T4");
}

TEST_F(TranslationTest, RecursiveDefinition) {
  default_edb_map["A"] = RelationInfo(1);
  default_edb_map["B"] = RelationInfo(1);
  default_edb_map["C"] = RelationInfo(1);

  EXPECT_EQ(TranslateDefinition("def Q {(x) : B(x) or exists ((y) | Q(y) and C(y))}"),
            "CREATE OR REPLACE VIEW Q AS (WITH RECURSIVE R0(A1) AS (SELECT T0.A1 AS x FROM B AS T0 UNION SELECT  FROM "
            "(SELECT T3.y FROM (SELECT T1.A1 AS y FROM R0 AS T1) AS T3, (SELECT T2.A1 AS y FROM C AS T2) AS T4 WHERE "
            "T3.y = T4.y) AS T5) SELECT DISTINCT T6.x AS A1 FROM (SELECT R0.A1 AS x FROM R0) AS T6);");
}

TEST_F(TranslationTest, TransitiveClosure) {
  default_edb_map["R"] = RelationInfo(2);

  EXPECT_EQ(TranslateDefinition("def Q {(x,y) : R(x,y) or exists((z) | R(x,z) and Q(z,y))}"),
            "CREATE OR REPLACE VIEW Q AS (WITH RECURSIVE R0(A1, A2) AS (SELECT T0.A1 AS x, T0.A2 AS y FROM R AS T0 "
            "UNION SELECT T5.x, T5.y FROM (SELECT T3.x, T3.z, T4.y FROM (SELECT T1.A1 AS x, T1.A2 AS z FROM R AS T1) "
            "AS T3, (SELECT T2.A1 AS z, T2.A2 AS y FROM R0 AS T2) AS T4 WHERE T3.z = T4.z) AS T5) SELECT DISTINCT T6.x "
            "AS A1, T6.y AS A2 FROM (SELECT R0.A1 AS x, R0.A2 AS y FROM R0) AS T6);");
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
}

TEST_F(TranslationTest, BindingEquality) {
  EXPECT_EQ(TranslateExpression("(x): x = 1"), "SELECT T0.name AS x FROM F AS T0");
}

// DomainToSql: direct tests for domain translation
TEST_F(TranslationTest, DomainToSqlConstantDomain) {
  ConstantDomain domain(42);
  EXPECT_EQ(DomainToSqlString(domain), "SELECT 42 AS A1");
}

TEST_F(TranslationTest, DomainToSqlDefinedDomain) {
  DefinedDomain domain("A", 1);
  EXPECT_EQ(DomainToSqlString(domain), "SELECT T0.A1 AS A1 FROM A AS T0");
}

TEST_F(TranslationTest, DomainToSqlProjection) {
  auto inner = std::make_unique<DefinedDomain>("B", 2);
  Projection proj({0}, std::move(inner));  // project first column only
  EXPECT_EQ(DomainToSqlString(proj), "SELECT T0.A1 AS A1 FROM B AS T0");
}

TEST_F(TranslationTest, DomainToSqlDomainUnion) {
  auto lhs = std::make_unique<DefinedDomain>("A", 1);
  auto rhs = std::make_unique<DefinedDomain>("D", 1);
  DomainUnion domain_union(std::move(lhs), std::move(rhs));
  EXPECT_EQ(DomainToSqlString(domain_union), "SELECT T0.A1 AS A1 FROM A AS T0 UNION SELECT T1.A1 AS A1 FROM D AS T1");
}

TEST_F(TranslationTest, DomainToSqlDomainOperation) {
  auto lhs = std::make_unique<ConstantDomain>(10);
  auto rhs = std::make_unique<ConstantDomain>(2);
  DomainOperation domain_op(std::move(lhs), std::move(rhs), RelTermOp::ADD);
  EXPECT_EQ(DomainToSqlString(domain_op), "SELECT 12 AS A1");
}

}  // namespace rel2sql
