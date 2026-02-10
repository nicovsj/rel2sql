#include "rel_ast/bound_set.h"

#include <unordered_set>

#include "gtest/gtest.h"
#include "rel_ast/bound.h"
#include "rel_ast/projection.h"

namespace rel2sql {

namespace {

// Build a single-projection bound (P) for one variable from a table.
Bound MakeSingleProjectionBound(const std::string& var, const std::string& table_name, size_t arity = 1) {
  std::unordered_set<Projection> domain;
  domain.insert(Projection({0}, TableSource(table_name, arity)));
  return Bound({var}, domain);
}

// Build a multi-projection bound (S) for one variable from two tables.
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

}  // namespace

TEST(SmallCoverTest, EmptyBounds) {
  BoundSet empty_set(std::unordered_set<Bound>{}, std::set<std::string>{});
  auto result = empty_set.SmallCover();
  EXPECT_TRUE(result.bounds.empty());
  EXPECT_TRUE(result.bound_variables.empty());
}

TEST(SmallCoverTest, PreferPOverS) {
  // x is covered by P_x (single proj from R) and S_x (union from A, B).
  // y is covered only by P_y (single proj from T).
  // Optimal cover: use P_x and P_y (cost 2). S_x should not be selected.
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

  // Optimal solution has cost 2 (two P's), so exactly 2 bounds.
  EXPECT_EQ(result.bounds.size(), 2u) << "Should select exactly P_x and P_y, not S_x";

  // Result should not contain the union bound S_x (which has two projections).
  for (const auto& b : result.bounds) {
    EXPECT_EQ(b.domain.size(), 1u) << "Selected bounds should be single-projection (P) only";
  }
}

TEST(SmallCoverTest, SingleVariableCoveredByPAndS) {
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

TEST(SmallCoverTest, OnlySCanCover) {
  // Variable x only covered by a union bound S (no single-projection bound for x).
  Bound S = MakeUnionProjectionBound("x", "A", "B");
  std::unordered_set<Bound> bounds = {S};
  BoundSet set(bounds, {"x"});

  auto result = set.SmallCover();

  EXPECT_TRUE(ResultCoversAllVariables(result));
  EXPECT_EQ(result.bounds.size(), 1u);
  EXPECT_EQ((*result.bounds.begin()).domain.size(), 2u);
}

TEST(SmallCoverTest, TwoVariablesTwoP) {
  Bound P_x = MakeSingleProjectionBound("x", "R");
  Bound P_y = MakeSingleProjectionBound("y", "T");
  std::unordered_set<Bound> bounds = {P_x, P_y};
  BoundSet set(bounds, {"x", "y"});

  auto result = set.SmallCover();

  EXPECT_TRUE(ResultCoversAllVariables(result));
  EXPECT_EQ(result.bounds.size(), 2u);
}

TEST(SmallCoverTest, EmptyBoundVariablesReturnsFullSet) {
  Bound P = MakeSingleProjectionBound("x", "R");
  std::unordered_set<Bound> bounds = {P};
  std::set<std::string> empty_vars;
  BoundSet set(bounds, empty_vars);

  auto result = set.SmallCover();

  EXPECT_EQ(result.bounds.size(), 1u);
  EXPECT_TRUE(result.bound_variables.empty());
}

}  // namespace rel2sql
