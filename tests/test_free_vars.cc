#include <gtest/gtest.h>

#include "parser/parse.h"

using namespace rel_parser;

TEST(FreeVarsTest, LitExpr) {
  auto ast = GetExtendedAST("def R { 1 }");

  EXPECT_EQ(ast.RootExtendedData().free_variables.size(), 0);
}

TEST(FreeVarsTest, VarExpr) {
  auto ast = GetExtendedAST("def R { x }");

  auto free_vars = ast.RootExtendedData().free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
}

TEST(FreeVarsTest, ProductExpr) {
  auto ast = GetExtendedAST("def R { x ; y }");

  auto free_vars = ast.RootExtendedData().free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}

TEST(FreeVarsTest, UnionExpr) {
  auto ast = GetExtendedAST("def R {(x, x, x)}");

  auto free_vars = ast.RootExtendedData().free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
}

TEST(FreeVarsTest, ExistenceQuantificationExpr) {
  auto ast = GetExtendedAST("def R { exists ((x) | F(x)) }");

  auto free_vars = ast.RootExtendedData().free_variables;

  EXPECT_EQ(free_vars.size(), 0);
}

TEST(FreeVarsTest, UniversalQuantificationExpr) {
  auto ast = GetExtendedAST("def R { forall ((x) | F(x)) }");

  auto free_vars = ast.RootExtendedData().free_variables;

  EXPECT_EQ(free_vars.size(), 0);
}

TEST(FreeVarsTest, ConjunctionExpr) {
  auto ast = GetExtendedAST("def R { F(x) and G(y) }");

  auto free_vars = ast.RootExtendedData().free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}

TEST(FreeVarsTest, DisjunctionExpr) {
  auto ast = GetExtendedAST("def R { F(x) or G(y) }");

  auto free_vars = ast.RootExtendedData().free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}

TEST(FreeVarsTest, NegationExpr) {
  auto ast = GetExtendedAST("def R { not F(x) }");

  auto free_vars = ast.RootExtendedData().free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
}

TEST(FreeVarsTest, BindingsExpr) {
  auto ast = GetExtendedAST("def R { [x, y]:  F[x, y] }");

  auto free_vars = ast.RootExtendedData().free_variables;

  EXPECT_EQ(free_vars.size(), 0);
}

TEST(FreeVarsTest, ConditionExpr) {
  auto ast = GetExtendedAST("def R { F[x] where G(y) }");

  auto free_vars = ast.RootExtendedData().free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}
