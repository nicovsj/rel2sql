#include <gtest/gtest.h>

#include <variant>

#include "translate.h"

namespace rel2sql {

TEST(SortedIDsTest, DefaultOrder) {
  std::string input = "def R{S}\ndef S{T}\n";
  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = GetExtendedASTFromTree(tree);
  auto data = ast.Data();
  std::vector<std::string> expected_order = {"S", "R"};
  EXPECT_EQ(data->sorted_ids, expected_order);
}

TEST(SortedIDsTest, ReverseOrder) {
  std::string input = "def S{T}\ndef R{S}\n";
  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = GetExtendedASTFromTree(tree);
  auto data = ast.Data();
  std::vector<std::string> expected_order = {"S", "R"};
  EXPECT_EQ(data->sorted_ids, expected_order);
}

TEST(SortedIDsTest, RandomOrder) {
  std::string input = "def S{T}\ndef R{S}\ndef T{1}\n";
  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = GetExtendedASTFromTree(tree);
  auto data = ast.Data();
  std::vector<std::string> expected_order = {"T", "S", "R"};
  EXPECT_EQ(data->sorted_ids, expected_order);
}

TEST(SortedIDsTest, BranchingOrder) {
  std::string input = "def S{T;U}\ndef R{S;T}\ndef T{U}\ndef U{1}\n";
  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = GetExtendedASTFromTree(tree);
  auto data = ast.Data();
  std::vector<std::string> expected_order = {"U", "T", "S", "R"};
  EXPECT_EQ(data->sorted_ids, expected_order);
}

TEST(SortedIDsTest, AmbiguousOrder) {
  std::string input = "def S{T;U}\ndef R{S;U}\ndef T{U}\ndef U{1}\n";
  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = GetExtendedASTFromTree(tree);
  auto data = ast.Data();
  std::vector<std::string> expected_order = {"U", "T", "S", "R"};
  EXPECT_EQ(data->sorted_ids, expected_order);
}

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
  auto ast = GetExtendedAST("def R { exists ((x) | x > 5) }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 0);
}

TEST(FreeVarsTest, UniversalQuantificationExpr) {
  auto ast = GetExtendedAST("def R { forall ((x) | x > 5) }");

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
  auto ast = GetExtendedAST("def R { not x > 5 }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
}

TEST(FreeVarsTest, BindingsExpr) {
  auto edb_map = rel2sql::edb_utils::FromArityMap({{"F", 2}});
  auto ast =
      GetExtendedASTFromTree(GetParser("def R { [x]:  F[x]}")->program(), std::make_shared<ExtendedASTData>(edb_map));

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 0);
}

TEST(FreeVarsTest, ConditionExpr) {
  auto edb_map = rel2sql::edb_utils::FromArityMap({{"F", 2}, {"G", 1}});
  auto ast = GetExtendedASTFromTree(GetParser("def R { F[x] where G(y) }")->program(),
                                    std::make_shared<ExtendedASTData>(edb_map));

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 2);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
  EXPECT_NE(free_vars.find("y"), free_vars.end());
}

TEST(FreeVarsTest, RelationTest) {
  auto ast = GetExtendedAST("def F { 1 }\ndef R { F; 1; x }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
}

TEST(FreeVarsTest, Terms) {
  auto ast = GetExtendedAST("def F { x+x < 5 }");

  auto free_vars = ast.Root().free_variables;

  EXPECT_EQ(free_vars.size(), 1);
  EXPECT_NE(free_vars.find("x"), free_vars.end());
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

TEST(LiteralVisitorTest, NumericalConstantInt) {
  std::string input = "1";

  auto parser = GetParser(input);

  auto tree = parser->numericalConstant();

  auto ast = GetExtendedASTFromTree(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<int>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<int>(ast.Root().constant.value()), 1);
}

TEST(ArityVisitorTest, LitExpr) {
  auto ast = GetExtendedAST("def R { 1 }");

  EXPECT_EQ(ast.Arity("R"), 1);
}

TEST(ArityVisitorTest, SingleVariable) {
  auto ast = GetExtendedAST("def R { x }");

  EXPECT_EQ(ast.Arity("R"), 1);
}

TEST(ArityVisitorTest, SimpleAbstraction) {
  auto ast = GetExtendedAST("def R { 1; 2; 3 }");

  EXPECT_EQ(ast.Arity("R"), 1);
}

TEST(ArityVisitorTest, Product) {
  auto ast = GetExtendedAST("def R { (1, 2) }");

  EXPECT_EQ(ast.Arity("R"), 2);
}

TEST(ArityVisitorTest, Abstraction) {
  auto ast = GetExtendedAST("def R {(1, 1); (2, 2); (3, 3)}");

  EXPECT_EQ(ast.Arity("R"), 2);
}

TEST(ArityVisitorTest, Formula) {
  // NOTE: Formulas are hardcoded to have 0-arity regardless of evaluation
  auto ast = GetExtendedAST("def R { F(1) and G(2,3) }");

  EXPECT_EQ(ast.Arity("R"), 0);
}

TEST(ArityVisitorTest, PartialApplication) {
  auto ast = GetExtendedAST("def R { F[1] }\n def F { (1, 2, 3) }");

  EXPECT_EQ(ast.Arity("R"), 2);
}

TEST(ArityVisitorTest, Binding) {
  std::unordered_map<std::string, int> arity_map = {{"F", 1}, {"G", 2}};
  auto edb_map = rel2sql::edb_utils::FromArityMap(arity_map);

  auto ast = GetExtendedASTFromTree(GetParser("def R { [x in F]: G[x] }")->program(),
                                    std::make_shared<ExtendedASTData>(edb_map));

  EXPECT_EQ(ast.Arity("R"), 2);
}

}  // namespace rel2sql
