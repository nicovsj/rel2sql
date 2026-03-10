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

// Returns true if the given Domain (or any domain in its tree) references the table.
bool DomainContainsTable(const Domain* d, const std::string& table_name) {
  if (!d) return false;
  if (auto* defined = dynamic_cast<const DefinedDomain*>(d)) {
    return defined->table_name == table_name;
  }
  if (auto* proj = dynamic_cast<const Projection*>(d)) {
    return DomainContainsTable(proj->domain.get(), table_name);
  }
  if (auto* un = dynamic_cast<const DomainUnion*>(d)) {
    return DomainContainsTable(un->lhs.get(), table_name) || DomainContainsTable(un->rhs.get(), table_name);
  }
  if (auto* op = dynamic_cast<const DomainOperation*>(d)) {
    return DomainContainsTable(op->lhs.get(), table_name) || DomainContainsTable(op->rhs.get(), table_name);
  }
  return false;
}

// Returns true if the BoundSet contains a bound whose domain references the given table.
bool BoundSetHasTable(const BoundSet& safety, const std::string& table_name) {
  for (const auto& bound : safety.bounds) {
    if (DomainContainsTable(bound.domain.get(), table_name)) return true;
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

  EXPECT_FALSE(exists->safety.bound_variables.count("x")) << "Existential should not bind x";
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
