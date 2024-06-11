#include <gtest/gtest.h>

#include "parser/generated/CoreRelLexer.h"
#include "parser/generated/PrunedCoreRelParser.h"
#include "src/parser/fv_visitor.h"

ExtendedAST get_extended_ast(std::string_view input) {
  antlr4::ANTLRInputStream input_stream(input.data());
  rel_parser::CoreRelLexer lexer(&input_stream);
  antlr4::CommonTokenStream tokens(&lexer);
  rel_parser::PrunedCoreRelParser parser(&tokens);

  rel_parser::PrunedCoreRelParser::ProgramContext* tree = parser.program();

  ExtendedASTVisitor visitor;

  return std::any_cast<ExtendedAST>(visitor.visitProgram(tree));
}

TEST(FreeVarsTest, LitExpr) {
  auto ast = get_extended_ast("def R { 1 }");

  EXPECT_EQ(ast.extended_data[ast.root].free_variables.size(), 0);
}

TEST(FreeVarsTest, VarExpr) {
  auto ast = get_extended_ast("def R { x }");

  auto free_vars = ast.extended_data[ast.root].free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
}

TEST(FreeVarsTest, ProductExpr) {
  auto ast = get_extended_ast("def R {x;y}");

  auto free_vars = ast.extended_data[ast.root].free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}

TEST(FreeVarsTest, UnionExpr) {
  auto ast = get_extended_ast("def R {(x, x, x)}");

  auto free_vars = ast.extended_data[ast.root].free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
}

TEST(FreeVarsTest, ExistenceQuantificationExpr) {
  auto ast = get_extended_ast("def R {exists (x) | F(x)}");

  auto free_vars = ast.extended_data[ast.root].free_variables;

  EXPECT_EQ(free_vars.size(), 0);
}

TEST(FreeVarsTest, UniversalQuantificationExpr) {
  auto ast = get_extended_ast("def R {forall (x) | F(x)}");

  auto free_vars = ast.extended_data[ast.root].free_variables;

  EXPECT_EQ(free_vars.size(), 0);
}

TEST(FreeVarsTest, ConjunctionExpr) {
  auto ast = get_extended_ast("def R {F(x) and G(y)}");

  auto free_vars = ast.extended_data[ast.root].free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}

TEST(FreeVarsTest, DisjunctionExpr) {
  auto ast = get_extended_ast("def R {F(x) or G(y)}");

  auto free_vars = ast.extended_data[ast.root].free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}

TEST(FreeVarsTest, NegationExpr) {
  auto ast = get_extended_ast("def R {not F(x)}");

  auto free_vars = ast.extended_data[ast.root].free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
}

TEST(FreeVarsTest, BindingsExpr) {
  auto ast = get_extended_ast("def R {[x, y]:  F[x, y]}");

  auto free_vars = ast.extended_data[ast.root].free_variables;

  EXPECT_EQ(free_vars.size(), 0);
}

TEST(FreeVarsTest, ConditionExpr) {
  auto ast = get_extended_ast("def R {F[x] where G(y)}");

  auto free_vars = ast.extended_data[ast.root].free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}
