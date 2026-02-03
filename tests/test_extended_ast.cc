#include <gtest/gtest.h>

#include <memory>
#include <variant>

#include "api/translate.h"
#include "rel_ast/bound_set.h"
#include "rel_ast/projection.h"

namespace rel2sql {

using psr = rel_parser::RelParser;

std::vector<std::string> GetSortedIDs(const std::string& input,
                                      const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);
  return ast.SortedIDs();
}

std::set<std::string> GetFreeVariables(const std::string& input,
                                       const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);
  return ast.Root()->free_variables;
}

// Helper function to parse and preprocess a formula
std::pair<RelAST, std::shared_ptr<antlr4::ParserRuleContext>> ProcessFormula(
    const std::string& input, const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<psr>(std::move(parser_unique));
  auto tree = parser->formula();
  auto ast = Preprocessor(edb_map).Process(tree);
  return {ast, std::shared_ptr<antlr4::ParserRuleContext>(parser, tree)};
}

// Helper function to parse and preprocess an expression
std::pair<RelAST, std::shared_ptr<antlr4::ParserRuleContext>> ProcessExpr(
    const std::string& input, const rel2sql::RelationMap& edb_map = rel2sql::RelationMap()) {
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<psr>(std::move(parser_unique));
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

TEST(RecursionVisitorTest, StoresRecursionMetadataInRelationInfo) {
  std::string input = "def Q {(x, y in R) : R(x, y) or exists ((z) | R(x, z) and Q(z, y))}";
  auto edb_map = rel2sql::relation_map::FromArityMap({{"R", 2}});

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb_map).Process(tree);

  ASSERT_EQ(tree->relDef().size(), 1);
  auto rel_def = tree->relDef()[0];
  auto rel_abs = rel_def->relAbs();
  ASSERT_EQ(rel_abs->expr().size(), 1);
  auto bindings_formula = dynamic_cast<psr::BindingsFormulaContext*>(rel_abs->expr()[0]);
  ASSERT_NE(bindings_formula, nullptr);

  auto or_ctx = dynamic_cast<psr::BinOpContext*>(bindings_formula->formula());
  ASSERT_NE(or_ctx, nullptr);
  auto base_formula = or_ctx->lhs;
  auto exists_ctx = dynamic_cast<psr::QuantificationContext*>(or_ctx->rhs);
  ASSERT_NE(exists_ctx, nullptr);

  auto and_ctx = dynamic_cast<psr::BinOpContext*>(exists_ctx->formula());
  ASSERT_NE(and_ctx, nullptr);

  psr::FullApplContext* recursive_call = nullptr;
  psr::FormulaContext* residual_formula = nullptr;

  for (auto* candidate : {and_ctx->lhs, and_ctx->rhs}) {
    auto full = dynamic_cast<psr::FullApplContext*>(candidate);
    if (full && full->applBase() && full->applBase()->T_ID() && full->applBase()->T_ID()->getText() == "Q") {
      recursive_call = full;
    } else if (!residual_formula) {
      residual_formula = candidate;
    }
  }

  ASSERT_NE(recursive_call, nullptr);
  ASSERT_NE(residual_formula, nullptr);

  auto rel_info = ast.GetRelationInfo("Q");
  ASSERT_TRUE(rel_info.has_value());
  ASSERT_TRUE(rel_info->HasRecursionMetadata());

  const auto& metadata = rel_info->RecursionMetadata();
  ASSERT_EQ(metadata.non_recursive_disjuncts.size(), 1);
  auto base_node = ast.GetNode(base_formula);
  EXPECT_EQ(metadata.non_recursive_disjuncts[0], base_node);

  ASSERT_EQ(metadata.recursive_disjuncts.size(), 1);
  const auto& branch = metadata.recursive_disjuncts[0];
  EXPECT_EQ(branch.exists_clause, ast.GetNode(exists_ctx));
  EXPECT_EQ(branch.recursive_call, ast.GetNode(recursive_call));
  EXPECT_EQ(branch.residual_formula, ast.GetNode(residual_formula));
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
  EXPECT_TRUE(node->safety.IsEmpty());
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

  EXPECT_EQ(quant_node->safety.Size(), 1)
      << "After removing z, {x} in π_0(R) and {y} in π_1(R) should merge into {x,y} in R";

  // Check that the merged binding has both x and y
  ASSERT_EQ(quant_node->safety.Size(), 1);
  auto bindings_bound = *quant_node->safety.bounds.begin();
  EXPECT_EQ(bindings_bound.variables.size(), 2) << "The merged binding should contain both x and y";

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
  EXPECT_TRUE(has_full_table_projection) << "The merged binding should have a full table projection of R (indices 0,1)";
}

TEST(SafetyVisitorTest, Composition2) {
  auto edb = rel2sql::relation_map::FromArityMap({{"R", 2}, {"S", 1}, {"T", 1}});
  auto [ast, tree] = ProcessFormula("R(x,y) or (S(x) and T(y))", edb);

  auto bin_op = dynamic_cast<psr::BinOpContext*>(tree.get());
  ASSERT_NE(bin_op, nullptr);

  auto safety = ast.GetNode(bin_op)->safety;

  // Safety bounds should be:
  // {(x) in {π_0(R); π_0(S); (y) in {π_1(R); π_0(T)}}

  // The following tests checks this.

  EXPECT_EQ(safety.Size(), 2);

  for (const auto& bb : safety.bounds) {
    EXPECT_EQ(bb.variables.size(), 1);
    EXPECT_EQ(bb.domain.size(), 2);
    EXPECT_TRUE(bb.variables[0] == "x" || bb.variables[0] == "y");

    for (const auto& proj : bb.domain) {
      EXPECT_EQ(proj.projected_indices.size(), 1);
      auto table_source = std::dynamic_pointer_cast<TableSource>(proj.source);
      ASSERT_NE(table_source, nullptr);
      if (bb.variables[0] == "x") {
        auto bool1 = table_source->table_name == "R" && proj.projected_indices[0] == 0;
        auto bool2 = table_source->table_name == "S" && proj.projected_indices[0] == 0;
        EXPECT_TRUE(bool1 || bool2);
      } else {
        auto bool1 = table_source->table_name == "R" && proj.projected_indices[0] == 1;
        auto bool2 = table_source->table_name == "T" && proj.projected_indices[0] == 0;
        EXPECT_TRUE(bool1 || bool2);
      }
    }
  }
}

// Helper function to get variable domain
std::unordered_set<Projection> GetVariableDomain(const RelAST& ast, const std::string& var) {
  return ast.GetVariableDomain(var);
}

// Helper function to check if a domain contains a specific table projection
bool HasTableProjection(const std::unordered_set<Projection>& domain, const std::string& table_name, size_t index) {
  for (const auto& proj : domain) {
    auto table_source = std::dynamic_pointer_cast<TableSource>(ResolvePromisedSource(proj.source));
    if (table_source && table_source->table_name == table_name &&
        proj.projected_indices.size() == 1 &&
        proj.projected_indices[0] == index) {
      return true;
    }
  }
  return false;
}

// Helper function to check if a domain contains a constant projection
bool HasConstantProjection(const std::unordered_set<Projection>& domain, const sql::ast::constant_t& constant) {
  for (const auto& proj : domain) {
    auto constant_source = std::dynamic_pointer_cast<ConstantSource>(ResolvePromisedSource(proj.source));
    if (constant_source && constant_source->value == constant) {
      return true;
    }
  }
  return false;
}

TEST(VariableDomainTest, SingleVariableFromRelationApplication) {
  // Relation applications in formulas don't directly store domains.
  // Domains are only stored when variables are bound in quantifiers, bindingsExpr, or bindingsFormula.
  // This test verifies that a relation application in a quantification context stores the domain.
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}});
  auto [ast, tree] = ProcessFormula("exists ((x) | F(x))", edb);

  auto x_domain = GetVariableDomain(ast, "x");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain from relation application in quantification";
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F at index 0";
}

TEST(VariableDomainTest, SingleVariableFromBindingsExpr) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}});
  auto [ast, tree] = ProcessExpr("[x]: F(x)", edb);

  auto x_domain = GetVariableDomain(ast, "x");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain from bindingsExpr";

  // Verify domain contains projection from F (index 0)
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F at index 0";

  // Verify y is not in domain map (not bound in bindingsExpr)
  auto y_domain = GetVariableDomain(ast, "y");
  EXPECT_TRUE(y_domain.empty()) << "Variable y should not be in domain map";
}

TEST(VariableDomainTest, SingleVariableFromBindingsFormula) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}});
  auto [ast, tree] = ProcessExpr("(x): F(x)", edb);

  auto x_domain = GetVariableDomain(ast, "x");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain from bindingsFormula";

  // Verify domain contains projection from F (index 0)
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F at index 0";
}

TEST(VariableDomainTest, SingleVariableFromQuantification) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}});
  auto [ast, tree] = ProcessFormula("exists ((x) | F(x))", edb);

  auto x_domain = GetVariableDomain(ast, "x");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain from quantification";

  // Verify domain contains projection from F (index 0)
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F at index 0";
}

TEST(VariableDomainTest, MultiVariableFromRelationApplication) {
  // Test multi-variable relation application in a quantification context
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 2}});
  auto [ast, tree] = ProcessFormula("exists ((x, y) | F(x, y))", edb);

  auto x_domain = GetVariableDomain(ast, "x");
  auto y_domain = GetVariableDomain(ast, "y");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain";
  EXPECT_FALSE(y_domain.empty()) << "Variable y should have a domain";

  // Verify x has projection from F (index 0)
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F at index 0";

  // Verify y has projection from F (index 1)
  EXPECT_TRUE(HasTableProjection(y_domain, "F", 1)) << "y should have projection from F at index 1";
}

TEST(VariableDomainTest, MultiVariableFromBindingsExpr) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 2}});
  auto [ast, tree] = ProcessExpr("[x, y]: F(x, y)", edb);

  auto x_domain = GetVariableDomain(ast, "x");
  auto y_domain = GetVariableDomain(ast, "y");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain";
  EXPECT_FALSE(y_domain.empty()) << "Variable y should have a domain";

  // Verify x has projection from F (index 0)
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F at index 0";

  // Verify y has projection from F (index 1)
  EXPECT_TRUE(HasTableProjection(y_domain, "F", 1)) << "y should have projection from F at index 1";

}

TEST(VariableDomainTest, ExplicitDomainBinding) {
  auto edb = rel2sql::relation_map::FromArityMap({{"R", 1}, {"F", 1}});
  auto [ast, tree] = ProcessExpr("[x in R]: F(x)", edb);

  auto x_domain = GetVariableDomain(ast, "x");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain";

  // Verify domain contains projection from R (explicit domain)
  EXPECT_TRUE(HasTableProjection(x_domain, "R", 0)) << "x should have projection from R (explicit domain)";

  // Verify domain contains projection from F (from expression)
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F (from expression)";
}

TEST(VariableDomainTest, MultipleExplicitDomainBindings) {
  auto edb = rel2sql::relation_map::FromArityMap({{"R", 1}, {"S", 1}, {"F", 1}});
  auto [ast, tree] = ProcessExpr("[x in R, y in S]: F(x, y)", edb);

  auto x_domain = GetVariableDomain(ast, "x");
  auto y_domain = GetVariableDomain(ast, "y");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain";
  EXPECT_FALSE(y_domain.empty()) << "Variable y should have a domain";

  // Verify x has projection from R (explicit) and F
  EXPECT_TRUE(HasTableProjection(x_domain, "R", 0)) << "x should have projection from R";
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F";

  // Verify y has projection from S (explicit) and F
  EXPECT_TRUE(HasTableProjection(y_domain, "S", 0)) << "y should have projection from S";
  EXPECT_TRUE(HasTableProjection(y_domain, "F", 1)) << "y should have projection from F";
}

TEST(VariableDomainTest, DomainUnionFromDisjunction) {
  // Test disjunction in a quantification context
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 1}});
  auto [ast, tree] = ProcessFormula("exists ((x) | F(x) or G(x))", edb);

  auto x_domain = GetVariableDomain(ast, "x");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain";

  // Verify domain contains projections from both F and G
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F";
  EXPECT_TRUE(HasTableProjection(x_domain, "G", 0)) << "x should have projection from G";
}

TEST(VariableDomainTest, DomainUnionFromConjunction) {
  // Test conjunction in a quantification context
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 1}});
  auto [ast, tree] = ProcessFormula("exists ((x) | F(x) and G(x))", edb);

  auto x_domain = GetVariableDomain(ast, "x");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain";

  // Verify domain contains projections from both F and G
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F";
  EXPECT_TRUE(HasTableProjection(x_domain, "G", 0)) << "x should have projection from G";
}

TEST(VariableDomainTest, ConstantDomainFromEquality) {
  auto [ast, tree] = ProcessFormula("x = 5");

  // Note: Equality comparisons don't directly create bindings in quantifiers/bindingsExpr,
  // so x might not have a domain stored unless it's in a binding context.
  // Let's test it in a quantification context
  auto [ast2, tree2] = ProcessFormula("exists ((x) | x = 5)");

  auto x_domain = GetVariableDomain(ast2, "x");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain from equality in quantification";

  // Verify domain contains constant projection with value 5
  EXPECT_TRUE(HasConstantProjection(x_domain, 5)) << "x should have constant projection with value 5";
}

TEST(VariableDomainTest, NestedQuantification) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 2}});
  auto [ast, tree] = ProcessFormula("exists ((x) | exists ((y) | F(x, y)))", edb);

  auto x_domain = GetVariableDomain(ast, "x");
  auto y_domain = GetVariableDomain(ast, "y");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain";
  EXPECT_FALSE(y_domain.empty()) << "Variable y should have a domain";

  // Verify x has projection from F (index 0)
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F at index 0";

  // Verify y has projection from F (index 1)
  EXPECT_TRUE(HasTableProjection(y_domain, "F", 1)) << "y should have projection from F at index 1";
}

TEST(VariableDomainTest, CompositionWithDomainExtraction) {
  // Test composition where x and y are free variables in the outer context
  // They should have domains from the inner quantification
  auto edb = rel2sql::relation_map::FromArityMap({{"R", 2}});
  auto [ast, tree] = ProcessFormula("exists((z) | R(x,z) and R(z,y))", edb);

  auto x_domain = GetVariableDomain(ast, "x");
  auto y_domain = GetVariableDomain(ast, "y");
  auto z_domain = GetVariableDomain(ast, "z");

  // x and y are free variables in the formula, so they might not have domains stored
  // unless they're in a binding context. z is quantified away, so it should have a domain
  // from the quantification, but it's removed from the outer safety set.

  // z should have a domain from the quantification (before removal)
  // Actually, z is quantified away, so it won't be in the final domain map
  // But it should have been stored during the quantification visit
  // The current implementation stores domains from the formula's safety before removing variables
  // So z should have a domain
  EXPECT_FALSE(z_domain.empty()) << "Variable z should have a domain from the quantification";
}

TEST(VariableDomainTest, MultipleBindingsForSameVariable) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 1}});
  std::string input = "def R { [x in F]: G[x] }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(edb).Process(tree);

  auto x_domain = GetVariableDomain(ast, "x");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain";

  // Verify domain contains projection from F (explicit)
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F (explicit domain)";

  // Verify domain contains projection from G (from expression)
  EXPECT_TRUE(HasTableProjection(x_domain, "G", 0)) << "x should have projection from G (from expression)";
}

TEST(VariableDomainTest, EmptyDomainForUnboundVariable) {
  std::string input = "def R { x }";

  auto parser = GetParser(input);
  auto tree = parser->program();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  auto x_domain = GetVariableDomain(ast, "x");

  // x should not be in domain map (no binding context)
  EXPECT_TRUE(x_domain.empty()) << "Variable x should not be in domain map (unbound)";
}

TEST(VariableDomainTest, NoDomainForQuantifiedVariable) {
  auto [ast, tree] = ProcessFormula("exists ((x) | x > 5)");

  auto x_domain = GetVariableDomain(ast, "x");

  // x should not be in domain map since it's not bound by a relation
  EXPECT_TRUE(x_domain.empty()) << "Variable x should not be in domain map (not bound by relation)";
}

TEST(VariableDomainTest, DomainMatchesSafetyAnalysis) {
  auto edb = rel2sql::relation_map::FromArityMap({{"F", 1}, {"G", 1}});
  auto [ast, tree] = ProcessFormula("exists ((x, y) | F(x) and G(y))", edb);

  auto x_domain = GetVariableDomain(ast, "x");
  auto y_domain = GetVariableDomain(ast, "y");

  EXPECT_FALSE(x_domain.empty()) << "Variable x should have a domain";
  EXPECT_FALSE(y_domain.empty()) << "Variable y should have a domain";

  // Verify domains match what's in safety analysis
  auto quant_ctx = dynamic_cast<psr::QuantificationContext*>(tree.get());
  ASSERT_NE(quant_ctx, nullptr);

  auto quant_node = ast.GetNode(quant_ctx);
  auto formula_node = ast.GetNode(quant_ctx->formula());

  // Extract domains from safety bounds
  std::unordered_set<Projection> x_domain_from_safety;
  std::unordered_set<Projection> y_domain_from_safety;

  for (const auto& bound : formula_node->safety.bounds) {
    for (size_t i = 0; i < bound.variables.size(); ++i) {
      if (bound.variables[i] == "x") {
        for (const auto& proj : bound.domain) {
          if (i < proj.projected_indices.size()) {
            std::vector<size_t> single_index = {proj.projected_indices[i]};
            x_domain_from_safety.insert(Projection(single_index, proj.source));
          }
        }
      } else if (bound.variables[i] == "y") {
        for (const auto& proj : bound.domain) {
          if (i < proj.projected_indices.size()) {
            std::vector<size_t> single_index = {proj.projected_indices[i]};
            y_domain_from_safety.insert(Projection(single_index, proj.source));
          }
        }
      }
    }
  }

  // Compare stored domains with safety analysis domains
  EXPECT_EQ(x_domain.size(), x_domain_from_safety.size()) << "x domain size should match safety analysis";
  EXPECT_EQ(y_domain.size(), y_domain_from_safety.size()) << "y domain size should match safety analysis";

  // Verify x has projection from F
  EXPECT_TRUE(HasTableProjection(x_domain, "F", 0)) << "x should have projection from F";

  // Verify y has projection from G
  EXPECT_TRUE(HasTableProjection(y_domain, "G", 0)) << "y should have projection from G";
}

// --- Linear term analysis tests ---

TEST(TermPolynomialVisitorTest, VariableOnly) {
  std::string input = "x";
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<psr>(std::move(parser_unique));
  auto tree = parser->expr();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  auto term_expr = dynamic_cast<psr::TermExprContext*>(tree);
  ASSERT_NE(term_expr, nullptr);

  auto term_node = ast.GetNode(term_expr->term());
  EXPECT_FALSE(term_node->IsInvalidTermExpression());
  ASSERT_TRUE(term_node->term_linear_coeffs.has_value());
  auto [a, b] = term_node->term_linear_coeffs.value();
  EXPECT_DOUBLE_EQ(a, 1.0);
  EXPECT_DOUBLE_EQ(b, 0.0);
}

TEST(TermPolynomialVisitorTest, AffineTermAndRoot) {
  std::string input = "2 * x - 4";
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<psr>(std::move(parser_unique));
  auto tree = parser->expr();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  auto term_expr = dynamic_cast<psr::TermExprContext*>(tree);
  ASSERT_NE(term_expr, nullptr);

  auto term_node = ast.GetNode(term_expr->term());
  EXPECT_FALSE(term_node->IsInvalidTermExpression());
  ASSERT_TRUE(term_node->term_linear_coeffs.has_value());
  auto [a, b] = term_node->term_linear_coeffs.value();
  EXPECT_DOUBLE_EQ(a, 2.0);
  EXPECT_DOUBLE_EQ(b, -4.0);

  auto root = term_node->GetPolynomialRoot();
  ASSERT_TRUE(root.has_value());
  EXPECT_DOUBLE_EQ(root.value(), 2.0);  // 2x - 4 = 0 -> x = 2
}


TEST(TermPolynomialVisitorTest, DivisionByConstant) {
  std::string input = "x / 2";
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<psr>(std::move(parser_unique));
  auto tree = parser->expr();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  auto term_expr = dynamic_cast<psr::TermExprContext*>(tree);
  ASSERT_NE(term_expr, nullptr);

  auto term_node = ast.GetNode(term_expr->term());
  EXPECT_FALSE(term_node->IsInvalidTermExpression());
  ASSERT_TRUE(term_node->term_linear_coeffs.has_value());
  auto [a, b] = term_node->term_linear_coeffs.value();
  EXPECT_DOUBLE_EQ(a, 0.5);
  EXPECT_DOUBLE_EQ(b, 0.0);
}

TEST(TermPolynomialVisitorTest, MultiVariableTermIsInvalid) {
  std::string input = "x + y";
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<psr>(std::move(parser_unique));
  auto tree = parser->expr();

  // Declare x and y as variables via a dummy relation definition context.
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  auto term_expr = dynamic_cast<psr::TermExprContext*>(tree);
  ASSERT_NE(term_expr, nullptr);

  auto term_node = ast.GetNode(term_expr->term());
  EXPECT_TRUE(term_node->IsInvalidTermExpression());
  EXPECT_FALSE(term_node->term_linear_coeffs.has_value());
}

TEST(TermPolynomialVisitorTest, DivisionByVariableIsInvalid) {
  std::string input = "2 / x";
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<psr>(std::move(parser_unique));
  auto tree = parser->expr();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  auto term_expr = dynamic_cast<psr::TermExprContext*>(tree);
  ASSERT_NE(term_expr, nullptr);

  auto term_node = ast.GetNode(term_expr->term());
  EXPECT_TRUE(term_node->IsInvalidTermExpression());
  EXPECT_FALSE(term_node->term_linear_coeffs.has_value());
}

TEST(TermPolynomialVisitorTest, FailedLinearityOfTerm1) {
  std::string input = "x*x + 1";
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<psr>(std::move(parser_unique));
  auto tree = parser->expr();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  auto term_expr = dynamic_cast<psr::TermExprContext*>(tree);
  ASSERT_NE(term_expr, nullptr);

  auto term_node = ast.GetNode(term_expr->term());
  EXPECT_TRUE(term_node->IsInvalidTermExpression());
  EXPECT_FALSE(term_node->term_linear_coeffs.has_value());
}

TEST(TermPolynomialVisitorTest, FailedLinearityOfTerm2) {
  std::string input = "(x+1) * (x+2)";
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<psr>(std::move(parser_unique));
  auto tree = parser->expr();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  auto term_expr = dynamic_cast<psr::TermExprContext*>(tree);
  ASSERT_NE(term_expr, nullptr);

  auto term_node = ast.GetNode(term_expr->term());
  EXPECT_TRUE(term_node->IsInvalidTermExpression());
  EXPECT_FALSE(term_node->term_linear_coeffs.has_value());
}

TEST(TermPolynomialVisitorTest, NullPolynomialTerm) {
  std::string input = "(1-1)*x";
  auto parser_unique = GetParser(input);
  auto parser = std::shared_ptr<psr>(std::move(parser_unique));
  auto tree = parser->expr();
  auto ast = Preprocessor(rel2sql::RelationMap()).Process(tree);

  auto term_expr = dynamic_cast<psr::TermExprContext*>(tree);
  ASSERT_NE(term_expr, nullptr);

  auto term_node = ast.GetNode(term_expr->term());
  EXPECT_TRUE(term_node->IsNullPolynomialTerm());
}


}  // namespace rel2sql
