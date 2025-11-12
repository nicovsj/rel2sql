#include <gtest/gtest.h>

#include <variant>

#include "translate.h"

namespace rel2sql {

std::vector<std::string> GetSortedIDs(const std::string& input, const rel2sql::EDBMap& edb_map = rel2sql::EDBMap()) {
  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);
  return ast.Data()->sorted_ids;
}

std::set<std::string> GetFreeVariables(const std::string& input, const rel2sql::EDBMap& edb_map = rel2sql::EDBMap()) {
  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);
  return ast.Root().free_variables;
}

TEST(SortedIDsTest, DefaultOrder) {
  std::string input = "def R{S}\ndef S{T}\n";
  std::vector<std::string> expected_order = {"S", "R"};

  EXPECT_EQ(GetSortedIDs(input), expected_order);
}

TEST(SortedIDsTest, ReverseOrder) {
  std::string input = "def S{T}\ndef R{S}\n";
  std::vector<std::string> expected_order = {"S", "R"};

  EXPECT_EQ(GetSortedIDs(input), expected_order);
}

TEST(SortedIDsTest, RandomOrder) {
  std::string input = "def S{T}\ndef R{S}\ndef T{1}\n";
  std::vector<std::string> expected_order = {"T", "S", "R"};
  EXPECT_EQ(GetSortedIDs(input), expected_order);
}

TEST(SortedIDsTest, BranchingOrder) {
  std::string input = "def S{T;U}\ndef R{S;T}\ndef T{U}\ndef U{1}\n";
  std::vector<std::string> expected_order = {"U", "T", "S", "R"};
  EXPECT_EQ(GetSortedIDs(input), expected_order);
}

TEST(SortedIDsTest, AmbiguousOrder) {
  std::string input = "def S{T;U}\ndef R{S;U}\ndef T{U}\ndef U{1}\n";
  std::vector<std::string> expected_order = {"U", "T", "S", "R"};
  EXPECT_EQ(GetSortedIDs(input), expected_order);
}

TEST(FreeVarsTest, LitExpr) {
  std::string input = "def R { 1 }";
  std::set<std::string> expected_free_vars = {};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(FreeVarsTest, VarExpr) {
  std::string input = "def R { x }";
  std::set<std::string> expected_free_vars = {"x"};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(FreeVarsTest, ProductExpr) {
  std::string input = "def R { x ; y }";

  std::set<std::string> expected_free_vars = {"x", "y"};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(FreeVarsTest, UnionExpr) {
  std::string input = "def R {(x, x, x)}";

  std::set<std::string> expected_free_vars = {"x"};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(FreeVarsTest, ExistenceQuantificationExpr) {
  std::string input = "def R { exists ((x) | x > 5) }";

  std::set<std::string> expected_free_vars = {};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(FreeVarsTest, UniversalQuantificationExpr) {
  std::string input = "def R { forall ((x) | x > 5) }";

  std::set<std::string> expected_free_vars = {};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(FreeVarsTest, ConjunctionExpr) {
  std::string input = "def R { F(x) and G(y) }";

  std::set<std::string> expected_free_vars = {"x", "y"};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(FreeVarsTest, DisjunctionExpr) {
  std::string input = "def R { F(x) or G(y) }";

  std::set<std::string> expected_free_vars = {"x", "y"};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(FreeVarsTest, NegationExpr) {
  std::string input = "def R { not x > 5 }";

  std::set<std::string> expected_free_vars = {"x"};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(FreeVarsTest, BindingsExpr) {
  std::string input = "def R {[x]: F[x]}";
  rel2sql::EDBMap edb_map = rel2sql::edb_utils::FromArityMap({{"F", 2}});

  std::set<std::string> expected_free_vars = {};

  EXPECT_EQ(GetFreeVariables(input, edb_map), expected_free_vars);
}

TEST(FreeVarsTest, RelationTest) {
  std::string input = "def F { 1 }\ndef R { F; 1; x }";

  std::set<std::string> expected_free_vars = {"x"};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(FreeVarsTest, Terms) {
  std::string input = "def F { x+x < 5 }";

  std::set<std::string> expected_free_vars = {"x"};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(LiteralVisitorTest, Int) {
  std::string input = "1";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<int>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<int>(ast.Root().constant.value()), 1);
}

TEST(LiteralVisitorTest, NegInt) {
  std::string input = "-1";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<int>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<int>(ast.Root().constant.value()), -1);
}

TEST(LiteralVisitorTest, Float) {
  std::string input = "1.0";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<double>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<double>(ast.Root().constant.value()), 1.0);
}

TEST(LiteralVisitorTest, NegFloat) {
  std::string input = "-1.0";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<double>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<double>(ast.Root().constant.value()), -1.0);
}

TEST(LiteralVisitorTest, Char) {
  std::string input = "'a'";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<std::string>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<std::string>(ast.Root().constant.value()), "a");
}

TEST(LiteralVisitorTest, Str) {
  std::string input = "\"abc\"";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<std::string>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<std::string>(ast.Root().constant.value()), "abc");
}

TEST(LiteralVisitorTest, Bool) {
  std::string input = "true";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<bool>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<bool>(ast.Root().constant.value()), true);
}

TEST(LiteralVisitorTest, BoolFalse) {
  std::string input = "false";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<bool>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<bool>(ast.Root().constant.value()), false);
}

TEST(LiteralVisitorTest, NumericalConstantInt) {
  std::string input = "1";

  auto parser = GetParser(input);

  auto tree = parser->numericalConstant();

  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_TRUE(ast.Root().constant.has_value());

  EXPECT_TRUE(std::holds_alternative<int>(ast.Root().constant.value()));

  EXPECT_EQ(std::get<int>(ast.Root().constant.value()), 1);
}

TEST(ArityVisitorTest, LitExpr) {
  std::string input = "def R { 1 }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_EQ(ast.Arity("R"), 1);
}

TEST(ArityVisitorTest, SingleVariable) {
  std::string input = "def R { x }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_EQ(ast.Arity("R"), 1);
}

TEST(ArityVisitorTest, SimpleAbstraction) {
  std::string input = "def R { 1; 2; 3 }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_EQ(ast.Arity("R"), 1);
}

TEST(ArityVisitorTest, Product) {
  std::string input = "def R { (1, 2) }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_EQ(ast.Arity("R"), 2);
}

TEST(ArityVisitorTest, Abstraction) {
  std::string input = "def R {(1, 1); (2, 2); (3, 3)}";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_EQ(ast.Arity("R"), 2);
}

TEST(ArityVisitorTest, Formula) {
  // NOTE: Formulas are hardcoded to have 0-arity regardless of evaluation
  std::string input = "def R { F(1) and G(2,3) }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_EQ(ast.Arity("R"), 0);
}

TEST(ArityVisitorTest, PartialApplication) {
  std::string input = "def R { F[1] }\n def F { (1, 2, 3) }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_EQ(ast.Arity("R"), 2);
}

TEST(ArityVisitorTest, Binding) {
  std::string input = "def R { [x in F]: G[x] }";
  rel2sql::EDBMap edb_map = rel2sql::edb_utils::FromArityMap({{"F", 1}, {"G", 2}});

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);

  EXPECT_EQ(ast.Arity("R"), 2);
}

TEST(ArityVisitorTest, DoubleDependency) {
  std::string input = "def X { (1, 2) }\ndef Y { (3, 4) }\ndef Z { X; Y }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::EDBMap()).Process(tree);

  EXPECT_EQ(ast.Arity("Z"), 2);
}

}  // namespace rel2sql
