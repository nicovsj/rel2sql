#include <gtest/gtest.h>

#include <variant>

#include "parser/parse.h"

using namespace rel_parser;

TEST(FreeVarsTest, LitExpr) {
  auto ast = GetExtendedAST("def R { 1 }");

  EXPECT_EQ(ast.Root().free_variables.size(), 0);
}

TEST(FreeVarsTest, VarExpr) {
  auto ast = GetExtendedAST("def R { x }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
}

TEST(FreeVarsTest, ProductExpr) {
  auto ast = GetExtendedAST("def R { x ; y }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}

TEST(FreeVarsTest, UnionExpr) {
  auto ast = GetExtendedAST("def R {(x, x, x)}");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
}

TEST(FreeVarsTest, ExistenceQuantificationExpr) {
  auto ast = GetExtendedAST("def R { exists ((x) | F(x)) }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 0);
}

TEST(FreeVarsTest, UniversalQuantificationExpr) {
  auto ast = GetExtendedAST("def R { forall ((x) | F(x)) }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 0);
}

TEST(FreeVarsTest, ConjunctionExpr) {
  auto ast = GetExtendedAST("def R { F(x) and G(y) }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}

TEST(FreeVarsTest, DisjunctionExpr) {
  auto ast = GetExtendedAST("def R { F(x) or G(y) }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}

TEST(FreeVarsTest, NegationExpr) {
  auto ast = GetExtendedAST("def R { not F(x) }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
}

TEST(FreeVarsTest, BindingsExpr) {
  auto ast = GetExtendedAST("def R { [x, y]:  F[x, y] }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 0);
}

TEST(FreeVarsTest, ConditionExpr) {
  auto ast = GetExtendedAST("def R { F[x] where G(y) }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}

TEST(LiteralVisitorTest, Int) {
  std::string input = "1";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = GetExtendedASTFromTree(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<int>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<int>(ast.Root().constant.value()), 1);
}

TEST(LiteralVisitorTest, NegInt) {
  std::string input = "-1";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = GetExtendedASTFromTree(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<int>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<int>(ast.Root().constant.value()), -1);
}

TEST(LiteralVisitorTest, Float) {
  std::string input = "1.0";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = GetExtendedASTFromTree(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<double>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<double>(ast.Root().constant.value()), 1.0);
}

TEST(LiteralVisitorTest, NegFloat) {
  std::string input = "-1.0";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = GetExtendedASTFromTree(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<double>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<double>(ast.Root().constant.value()), -1.0);
}

TEST(LiteralVisitorTest, Char) {
  std::string input = "'a'";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = GetExtendedASTFromTree(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<std::string>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<std::string>(ast.Root().constant.value()), "a");
}

TEST(LiteralVisitorTest, Str) {
  std::string input = "\"abc\"";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = GetExtendedASTFromTree(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<std::string>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<std::string>(ast.Root().constant.value()), "abc");
}

TEST(LiteralVisitorTest, Bool) {
  std::string input = "true";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = GetExtendedASTFromTree(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<bool>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<bool>(ast.Root().constant.value()), true);
}

TEST(LiteralVisitorTest, BoolFalse) {
  std::string input = "false";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = GetExtendedASTFromTree(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<bool>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<bool>(ast.Root().constant.value()), false);
}
