#include <unordered_set>

#include "api/translate.h"
#include "gtest/gtest.h"
#include "rel_ast/bound.h"
#include "rel_ast/bound_set.h"
#include "rel_ast/projection.h"
#include "rel_ast/rel_ast.h"
#include "rel_ast/rel_context.h"
#include "rel_ast/rel_context_builder.h"
#include "rel_ast/relation_info.h"

namespace rel2sql {

namespace {

// =============================================================================
// BoundSet helpers (from original test_bound_set.cc)
// =============================================================================

Bound MakeSingleProjectionBound(const std::string& var, const std::string& table_name, size_t arity = 1) {
  std::unordered_set<Projection> domain;
  domain.insert(Projection({0}, TableSource(table_name, arity)));
  return Bound({var}, domain);
}

Bound MakeUnionProjectionBound(const std::string& var, const std::string& table1, const std::string& table2,
                               size_t arity = 1) {
  std::unordered_set<Projection> domain;
  domain.insert(Projection({0}, TableSource(table1, arity)));
  domain.insert(Projection({0}, TableSource(table2, arity)));
  return Bound({var}, domain);
}

bool BoundContainsVariable(const Bound& b, const std::string& var) {
  return std::find(b.variables.begin(), b.variables.end(), var) != b.variables.end();
}

bool ResultCoversAllVariables(const BoundSet& result) {
  for (const auto& var : result.bound_variables) {
    bool covered = false;
    for (const auto& bound : result.bounds) {
      if (BoundContainsVariable(bound, var)) {
        covered = true;
        break;
      }
    }
    if (!covered) return false;
  }
  return true;
}

// Returns true if the BoundSet contains a bound whose domain has a projection from the given table.
bool BoundSetHasTable(const BoundSet& safety, const std::string& table_name) {
  for (const auto& bound : safety.bounds) {
    for (const auto& proj : bound.domain) {
      auto resolved = ResolvePromisedSource(proj.source);
      if (auto ts = std::dynamic_pointer_cast<TableSource>(resolved)) {
        if (ts->table_name == table_name) return true;
      }
    }
  }
  return false;
}

RelContext ProcessExpr(const std::string& rel_expr, const RelationMap& edb_map) {
  auto parser = GetParser(rel_expr);
  RelASTBuilder ast_builder;
  auto expr = ast_builder.BuildFromExpr(parser->expr());
  RelContextBuilder builder(edb_map);
  return builder.Process(expr);
}

RelContext ProcessFormula(const std::string& rel_formula, const RelationMap& edb_map) {
  auto parser = GetParser(rel_formula);
  RelASTBuilder ast_builder;
  auto formula = ast_builder.BuildFromFormula(parser->formula());
  RelContextBuilder builder(edb_map);
  return builder.Process(formula);
}

}  // namespace

// =============================================================================
// BoundSet / SmallCover tests (moved from test_bound_set.cc)
// =============================================================================

TEST(SafetyTest, SmallCoverEmptyBounds) {
  BoundSet empty_set(std::unordered_set<Bound>{}, std::set<std::string>{});
  auto result = empty_set.SmallCover();
  EXPECT_TRUE(result.bounds.empty());
  EXPECT_TRUE(result.bound_variables.empty());
}

TEST(SafetyTest, SmallCoverPreferPOverS) {
  Bound P_x = MakeSingleProjectionBound("x", "R");
  Bound S_x = MakeUnionProjectionBound("x", "A", "B");
  Bound P_y = MakeSingleProjectionBound("y", "T");

  std::unordered_set<Bound> bounds;
  bounds.insert(P_x);
  bounds.insert(S_x);
  bounds.insert(P_y);
  std::set<std::string> vars = {"x", "y"};
  BoundSet set(bounds, vars);

  auto result = set.SmallCover();

  EXPECT_TRUE(ResultCoversAllVariables(result)) << "Result must cover all variables";
  EXPECT_EQ(result.bound_variables.size(), 2u);
  EXPECT_EQ(result.bounds.size(), 2u) << "Should select exactly P_x and P_y, not S_x";
  for (const auto& b : result.bounds) {
    EXPECT_EQ(b.domain.size(), 1u) << "Selected bounds should be single-projection (P) only";
  }
}

TEST(SafetyTest, SmallCoverSingleVariableCoveredByPAndS) {
  Bound P = MakeSingleProjectionBound("x", "R");
  Bound S = MakeUnionProjectionBound("x", "A", "B");
  std::unordered_set<Bound> bounds = {P, S};
  BoundSet set(bounds, {"x"});

  auto result = set.SmallCover();

  EXPECT_TRUE(ResultCoversAllVariables(result));
  EXPECT_EQ(result.bounds.size(), 1u) << "Should pick one bound (the P)";
  EXPECT_EQ(result.bound_variables.size(), 1u);
  EXPECT_EQ((*result.bounds.begin()).domain.size(), 1u);
}

TEST(SafetyTest, SmallCoverOnlySCanCover) {
  Bound S = MakeUnionProjectionBound("x", "A", "B");
  std::unordered_set<Bound> bounds = {S};
  BoundSet set(bounds, {"x"});

  auto result = set.SmallCover();

  EXPECT_TRUE(ResultCoversAllVariables(result));
  EXPECT_EQ(result.bounds.size(), 1u);
  EXPECT_EQ((*result.bounds.begin()).domain.size(), 2u);
}

TEST(SafetyTest, SmallCoverTwoVariablesTwoP) {
  Bound P_x = MakeSingleProjectionBound("x", "R");
  Bound P_y = MakeSingleProjectionBound("y", "T");
  std::unordered_set<Bound> bounds = {P_x, P_y};
  BoundSet set(bounds, {"x", "y"});

  auto result = set.SmallCover();

  EXPECT_TRUE(ResultCoversAllVariables(result));
  EXPECT_EQ(result.bounds.size(), 2u);
}

TEST(SafetyTest, SmallCoverEmptyBoundVariablesReturnsFullSet) {
  Bound P = MakeSingleProjectionBound("x", "R");
  std::unordered_set<Bound> bounds = {P};
  std::set<std::string> empty_vars;
  BoundSet set(bounds, empty_vars);

  auto result = set.SmallCover();

  EXPECT_EQ(result.bounds.size(), 1u);
  EXPECT_TRUE(result.bound_variables.empty());
}

// =============================================================================
// SafetyInferrer tests - bottom-up and parent-to-child inheritance
// =============================================================================

TEST(SafetyTest, Conjunction) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);
  edb_map["B"] = RelationInfo(1);

  auto context = ProcessFormula("A(x) and B(x)", edb_map);

  auto* conj = dynamic_cast<RelConjunction*>(context.Root().get());
  ASSERT_NE(conj, nullptr);

  // Conjunction should have bounds for x from both A and B (bottom-up union)
  EXPECT_TRUE(conj->safety.bound_variables.count("x")) << "Conjunction should bind x";
  EXPECT_TRUE(BoundSetHasTable(conj->safety, "A")) << "Conjunction should have bound from A";
  EXPECT_TRUE(BoundSetHasTable(conj->safety, "B")) << "Conjunction should have bound from B";
}

TEST(SafetyTest, Disjunction) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);
  edb_map["B"] = RelationInfo(1);

  auto context = ProcessFormula("A(x) or B(x)", edb_map);

  auto* disj = dynamic_cast<RelDisjunction*>(context.Root().get());
  ASSERT_NE(disj, nullptr);

  EXPECT_TRUE(disj->safety.bound_variables.count("x")) << "Disjunction should bind x";
  EXPECT_TRUE(BoundSetHasTable(disj->safety, "A")) << "Disjunction should have bound from A";
  EXPECT_TRUE(BoundSetHasTable(disj->safety, "B")) << "Disjunction should have bound from B";
}

TEST(SafetyTest, Existential) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);

  auto context = ProcessFormula("exists ( (x) | A(x) )", edb_map);

  auto* exists = dynamic_cast<RelExistential*>(context.Root().get());
  ASSERT_NE(exists, nullptr);

  EXPECT_FALSE(exists->safety.bound_variables.count("x"))
      << "Existential should not bind x";
}

TEST(SafetyTest, FullApplication) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);

  auto context = ProcessFormula("A(x)", edb_map);

  auto* appl = dynamic_cast<RelFullApplication*>(context.Root().get());
  ASSERT_NE(appl, nullptr);

  EXPECT_TRUE(appl->safety.bound_variables.count("x")) << "A(x) should bind x";
  EXPECT_TRUE(BoundSetHasTable(appl->safety, "A"));
}

TEST(SafetyTest, Inheritance) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);
  edb_map["B"] = RelationInfo(1);

  auto context = ProcessFormula("not A(x) and B(x)", edb_map);

  auto* conj = dynamic_cast<RelConjunction*>(context.Root().get());
  ASSERT_NE(conj, nullptr);

  auto* lhs = dynamic_cast<RelNegation*>(conj->lhs.get());
  ASSERT_NE(lhs, nullptr);

  EXPECT_TRUE(lhs->safety.bound_variables.count("x")) << "LHS should bind x";
  EXPECT_TRUE(BoundSetHasTable(lhs->safety, "B")) << "LHS should have bound from B";
}

TEST(SafetyTest, ConditionInheritance) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);
  edb_map["B"] = RelationInfo(1);

  auto context = ProcessExpr("A(x) where B(x)", edb_map);

  auto* cond = dynamic_cast<RelCondition*>(context.Root().get());
  ASSERT_NE(cond, nullptr);

  // Condition should have bounds from both A and B
  EXPECT_TRUE(cond->safety.bound_variables.count("x"));
  EXPECT_TRUE(BoundSetHasTable(cond->safety, "A"));
  EXPECT_TRUE(BoundSetHasTable(cond->safety, "B"));

  // LHS (A(x) wrapped in RelFormulaAbstraction) should have inherited B's bound from the condition
  ASSERT_NE(cond->lhs, nullptr);
  EXPECT_TRUE(cond->lhs->safety.bound_variables.count("x"));
  EXPECT_TRUE(BoundSetHasTable(cond->lhs->safety, "A"));
  EXPECT_TRUE(BoundSetHasTable(cond->lhs->safety, "B")) << "LHS should inherit B's bound from condition (parent)";
}





}  // namespace rel2sql
