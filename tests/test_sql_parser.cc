// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "sql_parse.h"
#include "structs/sql_ast.h"

namespace rel2sql {

class SqlParserTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(SqlParserTest, ParseSimpleSelect) {
  std::string sql = "SELECT * FROM R;";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(expr);
  ASSERT_NE(select, nullptr);
  EXPECT_FALSE(select->is_distinct);
  EXPECT_EQ(select->columns.size(), 1);
}

TEST_F(SqlParserTest, ParseSelectWithColumns) {
  std::string sql = "SELECT A, B FROM R;";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(expr);
  ASSERT_NE(select, nullptr);
  EXPECT_EQ(select->columns.size(), 2);
}

TEST_F(SqlParserTest, ParseSelectWithWhere) {
  std::string sql = "SELECT * FROM R WHERE A = 1;";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(expr);
  ASSERT_NE(select, nullptr);
  ASSERT_TRUE(select->from.has_value());
  ASSERT_TRUE(select->from.value()->where.has_value());
}

TEST_F(SqlParserTest, ParseSelectWithAlias) {
  std::string sql = "SELECT A AS col1, B FROM R AS T1;";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(expr);
  ASSERT_NE(select, nullptr);
  EXPECT_EQ(select->columns.size(), 2);
}

TEST_F(SqlParserTest, ParseSelectDistinct) {
  std::string sql = "SELECT DISTINCT A FROM R;";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(expr);
  ASSERT_NE(select, nullptr);
  EXPECT_TRUE(select->is_distinct);
}

TEST_F(SqlParserTest, ParseSelectWithConstants) {
  std::string sql = "SELECT 1, 'hello', TRUE FROM R;";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(expr);
  ASSERT_NE(select, nullptr);
  EXPECT_EQ(select->columns.size(), 3);
}

TEST_F(SqlParserTest, ParseSelectWithGroupBy) {
  std::string sql = "SELECT A, COUNT(*) FROM R GROUP BY A;";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(expr);
  ASSERT_NE(select, nullptr);
  ASSERT_TRUE(select->group_by.has_value());
}

TEST_F(SqlParserTest, ParseValues) {
  std::string sql = "VALUES (1, 2), (3, 4);";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto values = std::dynamic_pointer_cast<sql::ast::Values>(expr);
  ASSERT_NE(values, nullptr);
  EXPECT_EQ(values->values.size(), 2);
  EXPECT_EQ(values->values[0].size(), 2);
}

TEST_F(SqlParserTest, ParseWithCTE) {
  std::string sql = "WITH T AS (SELECT * FROM R) SELECT * FROM T;";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(expr);
  ASSERT_NE(select, nullptr);
  EXPECT_FALSE(select->ctes.empty());
}

TEST_F(SqlParserTest, ParseUnion) {
  std::string sql = "SELECT * FROM R UNION SELECT * FROM S;";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto union_stmt = std::dynamic_pointer_cast<sql::ast::Union>(expr);
  ASSERT_NE(union_stmt, nullptr);
  EXPECT_EQ(union_stmt->members.size(), 2);
}

TEST_F(SqlParserTest, ParseUnionAll) {
  std::string sql = "SELECT * FROM R UNION ALL SELECT * FROM S;";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto union_all = std::dynamic_pointer_cast<sql::ast::UnionAll>(expr);
  ASSERT_NE(union_all, nullptr);
  EXPECT_EQ(union_all->members.size(), 2);
}

TEST_F(SqlParserTest, ParseSelectWhereEqualsConstant) {
  std::string sql = "SELECT * FROM A WHERE A.A1 = 1;";
  auto expr = ParseSQL(sql);
  std::string result = expr->ToString();

  ASSERT_TRUE(result.find("WHERE A.A1 = 1") != std::string::npos);
}

TEST_F(SqlParserTest, ParseCreateView) {
  std::string sql = "CREATE OR REPLACE VIEW R AS (SELECT * FROM T);";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto view = std::dynamic_pointer_cast<sql::ast::View>(expr);
  ASSERT_NE(view, nullptr);
  ASSERT_NE(view->source, nullptr);
  EXPECT_EQ(view->source->alias.has_value(), true);
  EXPECT_EQ(view->source->alias.value()->Access(), "R");
}

TEST_F(SqlParserTest, ParseCreateViewWithColumnList) {
  std::string sql = "CREATE OR REPLACE VIEW R (A1, A2) AS (SELECT A, B FROM T);";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto view = std::dynamic_pointer_cast<sql::ast::View>(expr);
  ASSERT_NE(view, nullptr);
  ASSERT_NE(view->source, nullptr);
  EXPECT_EQ(view->source->alias.value()->Access(), "R");
  EXPECT_EQ(view->source->def_columns.size(), 2);
  EXPECT_EQ(view->source->def_columns[0], "A1");
  EXPECT_EQ(view->source->def_columns[1], "A2");
}

TEST_F(SqlParserTest, ParseCreateViewWithValues) {
  std::string sql = "CREATE OR REPLACE VIEW R AS (SELECT DISTINCT * FROM (VALUES (1, 2), (3, 4)) AS T0 (A1, A2));";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto view = std::dynamic_pointer_cast<sql::ast::View>(expr);
  ASSERT_NE(view, nullptr);
  ASSERT_NE(view->source, nullptr);
  EXPECT_EQ(view->source->alias.value()->Access(), "R");

  // Verify the output string contains the expected elements
  std::string result = expr->ToString();
  ASSERT_TRUE(result.find("CREATE OR REPLACE VIEW") != std::string::npos);
  ASSERT_TRUE(result.find("VALUES") != std::string::npos);
}

TEST_F(SqlParserTest, ParseCreateTable) {
  std::string sql = "CREATE TABLE T AS (SELECT * FROM R);";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto create_table = std::dynamic_pointer_cast<sql::ast::CreateTable>(expr);
  ASSERT_NE(create_table, nullptr);
  ASSERT_NE(create_table->source, nullptr);
  EXPECT_EQ(create_table->source->alias.value()->Access(), "T");
}

TEST_F(SqlParserTest, ParseCreateTableWithColumnList) {
  std::string sql = "CREATE TABLE T (col1, col2) AS (SELECT A, B FROM R);";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto create_table = std::dynamic_pointer_cast<sql::ast::CreateTable>(expr);
  ASSERT_NE(create_table, nullptr);
  ASSERT_NE(create_table->source, nullptr);
  EXPECT_EQ(create_table->source->alias.value()->Access(), "T");
  EXPECT_EQ(create_table->source->def_columns.size(), 2);
  EXPECT_EQ(create_table->source->def_columns[0], "col1");
  EXPECT_EQ(create_table->source->def_columns[1], "col2");
}

TEST_F(SqlParserTest, ParseValuesSourceWithColumnList) {
  std::string sql = "SELECT * FROM (VALUES (1, 2), (3, 4)) AS T0 (A1, A2);";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(expr);
  ASSERT_NE(select, nullptr);
  ASSERT_TRUE(select->from.has_value());
  EXPECT_EQ(select->from.value()->sources.size(), 1);

  auto source = select->from.value()->sources[0];
  ASSERT_NE(source, nullptr);
  EXPECT_EQ(source->alias.value()->Access(), "T0");
  EXPECT_EQ(source->def_columns.size(), 2);
  EXPECT_EQ(source->def_columns[0], "A1");
  EXPECT_EQ(source->def_columns[1], "A2");
}

TEST_F(SqlParserTest, ParseSubquerySourceWithColumnList) {
  std::string sql = "SELECT * FROM (SELECT A, B FROM R) AS T (col1, col2);";
  auto expr = ParseSQL(sql);

  ASSERT_NE(expr, nullptr);

  auto select = std::dynamic_pointer_cast<sql::ast::SelectStatement>(expr);
  ASSERT_NE(select, nullptr);
  ASSERT_TRUE(select->from.has_value());
  EXPECT_EQ(select->from.value()->sources.size(), 1);

  auto source = select->from.value()->sources[0];
  ASSERT_NE(source, nullptr);
  EXPECT_EQ(source->alias.value()->Access(), "T");
  EXPECT_EQ(source->def_columns.size(), 2);
  EXPECT_EQ(source->def_columns[0], "col1");
  EXPECT_EQ(source->def_columns[1], "col2");
}

}  // namespace rel2sql
