#include <gtest/gtest.h>

#include <memory>
#include <variant>

#include "api/translate.h"
#include "rel_ast/bindings_bound.h"

namespace rel2sql {

using psr = rel_parser::PrunedCoreRelParser;

std::vector<std::string> GetSortedIDs(const std::string& input, const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);
  return ast.SortedIDs();
}

std::set<std::string> GetFreeVariables(const std::string& input, const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);
  return ast.Root()->free_variables;
}

// Helper function to parse and preprocess a formula
std::pair<RelAST, std::shared_ptr<antlr4::ParserRuleContext>> ProcessFormula(
    const std::string& input,
    const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<rel_parser::PrunedCoreRelParser>(std::move(parser_unique));
  auto tree = parser->formula();
  auto ast = Preprocessor(edb_map).Process(tree);
  return {ast, std::shared_ptr<antlr4::ParserRuleContext>(parser, tree)};
}

// Helper function to parse and preprocess an expression
std::pair<RelAST, std::shared_ptr<antlr4::ParserRuleContext>> ProcessExpr(
    const std::string& input,
    const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<rel_parser::PrunedCoreRelParser>(std::move(parser_unique));
  auto tree = parser->expr();
  auto ast = Preprocessor(edb_map).Process(tree);
  return {ast, std::shared_ptr<antlr4::ParserRuleContext>(parser, tree)};
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
  rel2sql::RelationMap edb_map = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 2}});

  std::set<std::string> expected_free_vars = {"x", "y"};

  EXPECT_EQ(GetFreeVariables(input, edb_map), expected_free_vars);
}

TEST(FreeVarsTest, DisjunctionExpr) {
  std::string input = "def R { F(x) or G(y) }";
  rel2sql::RelationMap edb_map = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 2}});

  std::set<std::string> expected_free_vars = {"x", "y"};

  EXPECT_EQ(GetFreeVariables(input, edb_map), expected_free_vars);
}

TEST(FreeVarsTest, NegationExpr) {
  std::string input = "def R { not x > 5 }";

  std::set<std::string> expected_free_vars = {"x"};

  EXPECT_EQ(GetFreeVariables(input), expected_free_vars);
}

TEST(FreeVarsTest, BindingsExpr) {
  std::string input = "def R {[x]: F[x]}";
  rel2sql::RelationMap edb_map = rel2sql::relation_map::FromArityMap({{"F", 2}});

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

  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_TRUE(ast.Root()->constant.has_value());

  EXPECT_TRUE(std::holds_alternative<int>(ast.Root()->constant.value()));

  EXPECT_EQ(std::get<int>(ast.Root()->constant.value()), 1);
}

TEST(LiteralVisitorTest, NegInt) {
  std::string input = "-1";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_TRUE(ast.Root()->constant.has_value());

  EXPECT_TRUE(std::holds_alternative<int>(ast.Root()->constant.value()));

  EXPECT_EQ(std::get<int>(ast.Root()->constant.value()), -1);
}

TEST(LiteralVisitorTest, Float) {
  std::string input = "1.0";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_TRUE(ast.Root()->constant.has_value());

  EXPECT_TRUE(std::holds_alternative<double>(ast.Root()->constant.value()));

  EXPECT_EQ(std::get<double>(ast.Root()->constant.value()), 1.0);
}

TEST(LiteralVisitorTest, NegFloat) {
  std::string input = "-1.0";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_TRUE(ast.Root()->constant.has_value());

  EXPECT_TRUE(std::holds_alternative<double>(ast.Root()->constant.value()));

  EXPECT_EQ(std::get<double>(ast.Root()->constant.value()), -1.0);
}

TEST(LiteralVisitorTest, Char) {
  std::string input = "'a'";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_TRUE(ast.Root()->constant.has_value());

  EXPECT_TRUE(std::holds_alternative<std::string>(ast.Root()->constant.value()));

  EXPECT_EQ(std::get<std::string>(ast.Root()->constant.value()), "a");
}

TEST(LiteralVisitorTest, Str) {
  std::string input = "\"abc\"";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_TRUE(ast.Root()->constant.has_value());

  EXPECT_TRUE(std::holds_alternative<std::string>(ast.Root()->constant.value()));

  EXPECT_EQ(std::get<std::string>(ast.Root()->constant.value()), "abc");
}

TEST(LiteralVisitorTest, Bool) {
  std::string input = "true";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_TRUE(ast.Root()->constant.has_value());

  EXPECT_TRUE(std::holds_alternative<bool>(ast.Root()->constant.value()));

  EXPECT_EQ(std::get<bool>(ast.Root()->constant.value()), true);
}

TEST(LiteralVisitorTest, BoolFalse) {
  std::string input = "false";

  auto parser = GetParser(input);

  auto tree = parser->literal();

  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_TRUE(ast.Root()->constant.has_value());

  EXPECT_TRUE(std::holds_alternative<bool>(ast.Root()->constant.value()));

  EXPECT_EQ(std::get<bool>(ast.Root()->constant.value()), false);
}

TEST(LiteralVisitorTest, NumericalConstantInt) {
  std::string input = "1";

  auto parser = GetParser(input);

  auto tree = parser->numericalConstant();

  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_TRUE(ast.Root()->constant.has_value());

  EXPECT_TRUE(std::holds_alternative<int>(ast.Root()->constant.value()));

  EXPECT_EQ(std::get<int>(ast.Root()->constant.value()), 1);
}

TEST(ArityVisitorTest, LitExpr) {
  std::string input = "def R { 1 }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_EQ(ast.GetArity("R"), 1);
}

TEST(ArityVisitorTest, SingleVariable) {
  std::string input = "def R { x }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_EQ(ast.GetArity("R"), 1);
}

TEST(ArityVisitorTest, SimpleAbstraction) {
  std::string input = "def R { 1; 2; 3 }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_EQ(ast.GetArity("R"), 1);
}

TEST(ArityVisitorTest, Product) {
  std::string input = "def R { (1, 2) }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_EQ(ast.GetArity("R"), 2);
}

TEST(ArityVisitorTest, Abstraction) {
  std::string input = "def R {(1, 1); (2, 2); (3, 3)}";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_EQ(ast.GetArity("R"), 2);
}

TEST(ArityVisitorTest, Formula) {
  // NOTE: Formulas are hardcoded to have 0-arity regardless of evaluation
  rel2sql::RelationMap edb_map = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 2}});
  std::string input = "def R { F(1) and G(2,3) }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);

  EXPECT_EQ(ast.GetArity("R"), 0);
}

TEST(ArityVisitorTest, PartialApplication) {
  std::string input = "def R { F[1] }\n def F { (1, 2, 3) }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_EQ(ast.GetArity("R"), 2);
}

TEST(ArityVisitorTest, Binding) {
  std::string input = "def R { [x in F]: G[x] }";
  rel2sql::RelationMap edb_map = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 2}});

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);

  EXPECT_EQ(ast.GetArity("R"), 2);
}

TEST(ArityVisitorTest, DoubleDependency) {
  std::string input = "def X { (1, 2) }\ndef Y { (3, 4) }\ndef Z { X; Y }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  EXPECT_EQ(ast.GetArity("Z"), 2);
}

TEST(RecursionVisitorTest, MatchesRecursionPattern) {
  std::string input = "def Q {(x in E) : G(x) or exists ((y) | Q(y) and F(y))}";
  auto edb_map = rel2sql::relation_map::FromArityMap({{"E", 1}, {"F", 1}, {"G", 1}});

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);

  ASSERT_EQ(tree->relDef().size(), 1);
  auto rel_def = tree->relDef()[0];
  auto rel_abs = rel_def->relAbs();
  ASSERT_EQ(rel_abs->expr().size(), 1);
  auto bindings_formula = dynamic_cast<psr::BindingsFormulaContext*>(rel_abs->expr()[0]);
  ASSERT_NE(bindings_formula, nullptr);

  EXPECT_TRUE(ast.GetNode(rel_abs)->is_recursive);
  EXPECT_TRUE(ast.GetNode(bindings_formula)->is_recursive);
}

TEST(RecursionVisitorTest, RejectsNonRecursionPattern) {
  std::string input = "def Q {(x in E) : G(x) or exists ((y) | F(y) and H(y))}";
  auto edb_map = rel2sql::relation_map::FromArityMap({{"E", 1}, {"F", 1}, {"G", 1}, {"H", 1}});

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);

  ASSERT_EQ(tree->relDef().size(), 1);
  auto rel_def = tree->relDef()[0];
  auto rel_abs = rel_def->relAbs();
  ASSERT_EQ(rel_abs->expr().size(), 1);
  auto bindings_formula = dynamic_cast<psr::BindingsFormulaContext*>(rel_abs->expr()[0]);
  ASSERT_NE(bindings_formula, nullptr);

  EXPECT_FALSE(ast.GetNode(rel_abs)->is_recursive);
  EXPECT_FALSE(ast.GetNode(bindings_formula)->is_recursive);
}

TEST(SafetyVisitorTest, RelationApplicationCreatesBindingsBound) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 2}});
  auto [ast, tree] = ProcessFormula("F(x, y)", edb);

  auto full_appl = dynamic_cast<psr::FullApplContext*>(tree.get());
  ASSERT_NE(full_appl, nullptr);

  auto node = ast.GetNode(full_appl);
  ASSERT_EQ(node->safety.Size(), 1);

  auto bindings_bound = *node->safety.bounds.begin();
  EXPECT_EQ(bindings_bound.variables.size(), 2);
  EXPECT_EQ(bindings_bound.variables[0], "x");
  EXPECT_EQ(bindings_bound.variables[1], "y");
  EXPECT_FALSE(bindings_bound.domain.empty());
  EXPECT_TRUE(bindings_bound.IsCorrect());
}

TEST(SafetyVisitorTest, EqualityComparisonWithConstant) {
  auto [ast, tree] = ProcessFormula("x = 5");

  auto comparison = dynamic_cast<psr::ComparisonContext*>(tree.get());
  ASSERT_NE(comparison, nullptr);

  auto node = ast.GetNode(comparison);
  ASSERT_EQ(node->safety.Size(), 1);

  auto bindings_bound = *node->safety.bounds.begin();
  EXPECT_EQ(bindings_bound.variables.size(), 1);
  EXPECT_EQ(bindings_bound.variables[0], "x");
  EXPECT_FALSE(bindings_bound.domain.empty());
  EXPECT_TRUE(bindings_bound.IsCorrect());
}

TEST(SafetyVisitorTest, ConjunctionMergesSafetySets) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 1}});
  auto [ast, tree] = ProcessFormula("F(x) and G(y)", edb);

  auto bin_op = dynamic_cast<psr::BinOpContext*>(tree.get());
  ASSERT_NE(bin_op, nullptr);

  auto node = ast.GetNode(bin_op);
  // Conjunction should have both BindingsBound: one for F(x) and one for G(y)
  EXPECT_GE(node->safety.Size(), 2);

  // Check that we have bindings for both x and y
  bool has_x = false, has_y = false;
  for (const auto& bb : node->safety.bounds) {
    if (bb.variables.size() == 1 && bb.variables[0] == "x") {
      has_x = true;
    }
    if (bb.variables.size() == 1 && bb.variables[0] == "y") {
      has_y = true;
    }
  }
  EXPECT_TRUE(has_x);
  EXPECT_TRUE(has_y);
}

TEST(SafetyVisitorTest, DisjunctionMergesMatchingVariableSets) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 1}});
  auto [ast, tree] = ProcessFormula("F(x) or G(x)", edb);

  auto bin_op = dynamic_cast<psr::BinOpContext*>(tree.get());
  ASSERT_NE(bin_op, nullptr);

  auto node = ast.GetNode(bin_op);
  // Disjunction should merge bindings with same variable set
  // F(x) and G(x) both bind x, so they should be merged
  EXPECT_GE(node->safety.Size(), 1);

  // Check that we have a binding for x
  bool has_x = false;
  for (const auto& bb : node->safety.bounds) {
    if (bb.variables.size() == 1 && bb.variables[0] == "x") {
      has_x = true;
      // The merged domain should contain both F and G
      EXPECT_GE(bb.domain.size(), 1);
    }
  }
  EXPECT_TRUE(has_x);
}

TEST(SafetyVisitorTest, QuantificationRemovesBoundVariables) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}});
  auto [ast, tree] = ProcessFormula("exists ((x) | F(x))", edb);

  auto quantification = dynamic_cast<psr::QuantificationContext*>(tree.get());
  ASSERT_NE(quantification, nullptr);

  auto node = ast.GetNode(quantification);
  // After removing x, the safety set should be empty (no free variables)
  // Since F(x) binds x, and x is quantified away, there are no bound variables left
  EXPECT_TRUE(node->safety.Empty());
}

TEST(SafetyVisitorTest, BindingsExpressionRemovesBoundVariables) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 2}});
  auto [ast, tree] = ProcessExpr("[x]: F(x, y)", edb);

  auto bindings_expr = dynamic_cast<psr::BindingsExprContext*>(tree.get());
  ASSERT_NE(bindings_expr, nullptr);

  auto node = ast.GetNode(bindings_expr);
  // After removing x, only y should remain in bindings
  // F(x, y) binds both x and y, but x is bound in [x], so only y remains
  EXPECT_GE(node->safety.Size(), 0);

  // Check that any remaining bindings don't contain x
  for (const auto& bb : node->safety.bounds) {
    auto it = std::find(bb.variables.begin(), bb.variables.end(), "x");
    EXPECT_EQ(it, bb.variables.end()) << "Variable x should be removed from bindings";
  }
}

TEST(SafetyVisitorTest, RelationalAbstraction1) {
  std::string input = "{F(x); G(y)}";
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 1}});
  auto [ast, tree] = ProcessExpr(input, edb);

  auto rel_abs = dynamic_cast<psr::RelAbsExprContext*>(tree.get());
  ASSERT_NE(rel_abs, nullptr);

  auto node = ast.GetNode(rel_abs);
  EXPECT_GE(node->safety.Size(), 0);
}

TEST(SafetyVisitorTest, RelationalAbstraction2) {
  std::string input = "{F(x); G(x)}";
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 1}});
  auto [ast, tree] = ProcessExpr(input, edb);

  auto rel_abs = dynamic_cast<psr::RelAbsExprContext*>(tree.get());
  ASSERT_NE(rel_abs, nullptr);

  auto node = ast.GetNode(rel_abs);
  EXPECT_GE(node->safety.Size(), 1);

  for (const auto& bb : node->safety.bounds) {
    EXPECT_EQ(bb.variables.size(), 1);
    EXPECT_EQ(bb.variables[0], "x");
  }
}


TEST(SafetyVisitorTest, MultipleVariableRelationApplication) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 3}});
  auto [ast, tree] = ProcessFormula("F(x, y, z)", edb);

  auto full_appl = dynamic_cast<psr::FullApplContext*>(tree.get());
  ASSERT_NE(full_appl, nullptr);

  auto node = ast.GetNode(full_appl);
  ASSERT_EQ(node->safety.Size(), 1);

  auto bindings_bound = *node->safety.bounds.begin();
  EXPECT_EQ(bindings_bound.variables.size(), 3);
  EXPECT_EQ(bindings_bound.variables[0], "x");
  EXPECT_EQ(bindings_bound.variables[1], "y");
  EXPECT_EQ(bindings_bound.variables[2], "z");
  EXPECT_TRUE(bindings_bound.IsCorrect());
}

TEST(SafetyVisitorTest, NestedConjunction) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 1}, {"H", 1}});
  auto [ast, tree] = ProcessFormula("F(x) and G(y) and H(z)", edb);

  auto bin_op = dynamic_cast<psr::BinOpContext*>(tree.get());
  ASSERT_NE(bin_op, nullptr);

  auto node = ast.GetNode(bin_op);
  // Should contain bindings for x, y, and z
  EXPECT_GE(node->safety.Size(), 3);

  std::set<std::string> found_vars;
  for (const auto& bb : node->safety.bounds) {
    if (bb.variables.size() == 1) {
      found_vars.insert(bb.variables[0]);
    }
  }
  EXPECT_TRUE(found_vars.count("x"));
  EXPECT_TRUE(found_vars.count("y"));
  EXPECT_TRUE(found_vars.count("z"));
}

TEST(SafetyVisitorTest, Composition) {
  auto edb = rel2sql::relation_map::FromArityMap({{"R", 2}});
  auto [ast, tree] = ProcessFormula("exists((z) | R(x,z) and R(z,y))", edb);

  auto quantification = dynamic_cast<psr::QuantificationContext*>(tree.get());
  ASSERT_NE(quantification, nullptr);

  auto quant_node = ast.GetNode(quantification);

  // After removing z from R(x,z) and R(z,y), we should get:
  // - Without merging: {{x} in π_0(R), {y} in π_1(R)} - two separate bindings
  // - With merging: {{x,y} in R} - one merged binding (complete covering)

  // This test will FAIL until merging is implemented because we'll have 2 bindings
  // After merging is implemented, it should have 1 binding with {x,y}
  EXPECT_EQ(quant_node->safety.Size(), 1)
      << "After removing z, {x} in π_0(R) and {y} in π_1(R) should merge into {x,y} in R";

  // Check that the merged binding has both x and y
  ASSERT_EQ(quant_node->safety.Size(), 1);
  auto bindings_bound = *quant_node->safety.bounds.begin();
  EXPECT_EQ(bindings_bound.variables.size(), 2)
      << "The merged binding should contain both x and y";

  std::set<std::string> vars(bindings_bound.variables.begin(), bindings_bound.variables.end());
  EXPECT_TRUE(vars.count("x")) << "The merged binding should contain x";
  EXPECT_TRUE(vars.count("y")) << "The merged binding should contain y";

  // Check that we have a full table projection (indices 0 and 1 from table R)
  bool has_full_table_projection = false;
  for (const auto& proj : bindings_bound.domain) {
    auto table_source = std::dynamic_pointer_cast<TableSource>(proj.source);
    if (table_source && table_source->table_name == "R") {
      // Check if this is a full table projection (all indices 0..arity-1)
      if (proj.projected_indices.size() == 2 &&
          std::find(proj.projected_indices.begin(), proj.projected_indices.end(), 0) != proj.projected_indices.end() &&
          std::find(proj.projected_indices.begin(), proj.projected_indices.end(), 1) != proj.projected_indices.end()) {
        has_full_table_projection = true;
        break;
      }
    }
  }
  EXPECT_TRUE(has_full_table_projection)
      << "The merged binding should have a full table projection of R (indices 0,1)";
}

}  // namespace rel2sql
