// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "sql_parse.h"
#include "structs/edb_info.h"
#include "test_common.h"
#include "translate.h"

namespace rel2sql {

class SqlParserRoundTripTest : public ::testing::Test {
 protected:
  void SetUp() override { default_edb_map = CreateDefaultEDBMap(); }

  // Helper function to perform round-trip test:
  // 1. Parse Rel query → get sql::ast (without optimization)
  // 2. Convert sql::ast → SQL string
  // 3. Parse SQL string → get new sql::ast
  // 4. Compare original and parsed sql::ast structures
  void TestRoundTripFormula(const std::string& rel_query) {
    auto parser = GetParser(rel_query);
    auto tree = parser->formula();

    Preprocessor preprocessor(default_edb_map);
    auto ast = preprocessor.Process(tree);
    auto original_sql_ast = GetSQLFromAST(ast);

    std::string sql_string = original_sql_ast->ToString();

    // Parse the SQL string back to sql::ast
    auto parsed_sql_ast = ParseSQL(sql_string, default_edb_map);

    // Compare the ASTs
    EXPECT_TRUE(AreASTsEqual(original_sql_ast, parsed_sql_ast))
        << "Round-trip failed for formula: " << rel_query << "\n"
        << "Original SQL: " << sql_string << "\n"
        << "Original AST string: " << original_sql_ast->ToString() << "\n"
        << "Parsed AST string: " << parsed_sql_ast->ToString();
  }

  void TestRoundTripExpression(const std::string& rel_query) {
    auto parser = GetParser(rel_query);
    auto tree = parser->expr();

    Preprocessor preprocessor(default_edb_map);
    auto ast = preprocessor.Process(tree);
    auto original_sql_ast = GetSQLFromAST(ast);

    std::string sql_string = original_sql_ast->ToString();

    // Parse the SQL string back to sql::ast
    auto parsed_sql_ast = ParseSQL(sql_string, default_edb_map);

    // Compare the ASTs
    EXPECT_TRUE(AreASTsEqual(original_sql_ast, parsed_sql_ast))
        << "Round-trip failed for expression: " << rel_query << "\n"
        << "Original SQL: " << sql_string << "\n"
        << "Original AST string: " << original_sql_ast->ToString() << "\n"
        << "Parsed AST string: " << parsed_sql_ast->ToString();
  }

  void TestRoundTripDefinition(const std::string& rel_query) {
    auto parser = GetParser(rel_query);
    auto tree = parser->relDef();

    Preprocessor preprocessor(default_edb_map);
    auto ast = preprocessor.Process(tree);
    auto original_sql_ast = GetSQLFromAST(ast);

    std::string sql_string = original_sql_ast->ToString();

    // Parse the SQL string back to sql::ast
    auto parsed_sql_ast = ParseSQL(sql_string, default_edb_map);

    // Compare the ASTs
    EXPECT_TRUE(AreASTsEqual(original_sql_ast, parsed_sql_ast))
        << "Round-trip failed for definition: " << rel_query << "\n"
        << "Original SQL: " << sql_string << "\n"
        << "Original AST string: " << original_sql_ast->ToString() << "\n"
        << "Parsed AST string: " << parsed_sql_ast->ToString();
  }

  EDBMap default_edb_map;
};

// Simple formula tests
TEST_F(SqlParserRoundTripTest, FullApplicationFormula) {
  TestRoundTripFormula("A(x)");
}

TEST_F(SqlParserRoundTripTest, FullApplicationFormulaMultipleParams1) {
  TestRoundTripFormula("B(x, y)");
}

TEST_F(SqlParserRoundTripTest, FullApplicationFormulaMultipleParams2) {
  TestRoundTripFormula("C(x, y, z)");
}

TEST_F(SqlParserRoundTripTest, RepeatedVariableFormula1) {
  TestRoundTripFormula("B(x, x)");
}

TEST_F(SqlParserRoundTripTest, RepeatedVariableFormula2) {
  TestRoundTripFormula("C(x, x, x)");
}

TEST_F(SqlParserRoundTripTest, RepeatedVariableFormula3) {
  TestRoundTripFormula("C(x, y, x)");
}

TEST_F(SqlParserRoundTripTest, OperatorFormula) {
  TestRoundTripFormula("A(x) and x*x > 5");
}

TEST_F(SqlParserRoundTripTest, ConjunctionFormula) {
  TestRoundTripFormula("A(x) and D(x)");
}

TEST_F(SqlParserRoundTripTest, DisjunctionFormula) {
  TestRoundTripFormula("A(x) or D(x)");
}

// Existential formula tests
TEST_F(SqlParserRoundTripTest, ExistentialFormula1) {
  TestRoundTripFormula("exists ((y) | B(x, y))");
}

TEST_F(SqlParserRoundTripTest, ExistentialFormula2) {
  TestRoundTripFormula("exists ((y, z) | C(x, y, z))");
}

TEST_F(SqlParserRoundTripTest, ExistentialFormula3) {
  TestRoundTripFormula("exists ((y in A) | B(x, y))");
}

TEST_F(SqlParserRoundTripTest, ExistentialFormula4) {
  TestRoundTripFormula("exists ((y in A, z in D) | C(x, y, z))");
}

TEST_F(SqlParserRoundTripTest, ExistentialFormula5) {
  TestRoundTripFormula("exists ((y in A, z) | C(x, y, z))");
}

// Expression tests
TEST_F(SqlParserRoundTripTest, ProductExpression) {
  TestRoundTripExpression("(1, 2)");
}

TEST_F(SqlParserRoundTripTest, ConditionExpression) {
  TestRoundTripExpression("B[x] where A(x)");
}

TEST_F(SqlParserRoundTripTest, PartialApplication1) {
  TestRoundTripExpression("B[x]");
}

TEST_F(SqlParserRoundTripTest, PartialApplication2) {
  TestRoundTripExpression("B[1]");
}

TEST_F(SqlParserRoundTripTest, NestedPartialApplication1) {
  TestRoundTripExpression("B[E[x]]");
}

TEST_F(SqlParserRoundTripTest, PartialApplicationMixedParams1) {
  TestRoundTripExpression("C[B[x], y]");
}

TEST_F(SqlParserRoundTripTest, PartialApplicationMixedParams2) {
  TestRoundTripExpression("C[x, 1]");
}

TEST_F(SqlParserRoundTripTest, PartialApplicationMixedParams3) {
  TestRoundTripExpression("C[B[x], E[y]]");
}

TEST_F(SqlParserRoundTripTest, PartialApplicationSharingVariables1) {
  TestRoundTripExpression("C[B[x], E[x]]");
}

TEST_F(SqlParserRoundTripTest, PartialApplicationSharingVariables3) {
  TestRoundTripExpression("C[B[x], x]");
}

// Aggregate expression tests
TEST_F(SqlParserRoundTripTest, AggregateExpression1) {
  TestRoundTripExpression("sum[A]");
}

TEST_F(SqlParserRoundTripTest, AggregateExpression2) {
  TestRoundTripExpression("average[A]");
}

TEST_F(SqlParserRoundTripTest, AggregateExpression3) {
  TestRoundTripExpression("min[A]");
}

TEST_F(SqlParserRoundTripTest, AggregateExpression4) {
  TestRoundTripExpression("max[A]");
}

TEST_F(SqlParserRoundTripTest, AggregateExpression5) {
  TestRoundTripExpression("max[B[x]]");
}

// Binding expression tests
TEST_F(SqlParserRoundTripTest, BindingExpression) {
  TestRoundTripExpression("[x in A, y in D]: C[x, y]");
}

TEST_F(SqlParserRoundTripTest, BindingExpressionBounded) {
  TestRoundTripExpression("[x in A, y]: C[x, y] where D(y)");
}

TEST_F(SqlParserRoundTripTest, BindingFormula) {
  TestRoundTripExpression("[x in A, y in D]: B(x, y)");
}

// Definition tests
TEST_F(SqlParserRoundTripTest, SimpleDefinition) {
  TestRoundTripDefinition("def R {A}");
}

TEST_F(SqlParserRoundTripTest, TableDefinition) {
  TestRoundTripDefinition("def R {(1, 2); (3, 4)}");
}

TEST_F(SqlParserRoundTripTest, ProgramDefinition) {
  TestRoundTripDefinition("def R {[x in A]: B[x]}");
}

}  // namespace rel2sql
