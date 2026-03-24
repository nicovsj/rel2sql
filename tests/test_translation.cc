// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "api/translate.h"
#include "rel_ast/domain.h"
#include "rel_ast/rel_ast.h"
#include "rel_ast/relation_info.h"
#include "sql/translator.h"
#include "test_common.h"

namespace rel2sql {

namespace {

RelContext BuildContextFromFormula(const std::string& formula, const RelationMap& edb_map) {
  auto parser = GetParser(formula);
  auto tree = parser->formula();
  RelASTBuilder ast_builder;
  auto root = ast_builder.BuildFromFormula(tree);
  RelContextBuilder builder(edb_map);
  return builder.Process(root);
}

}  // namespace

class TranslationTest : public ::testing::Test {
 protected:
  // Translate a Domain to SQL string. Domains are independent of the input formula;
  // a minimal context (A(x) with default EDB) is used internally. Applies optimizer.
  std::string DomainToSqlString(const Domain& domain) {
    auto context = BuildContextFromFormula("A(x)", CreateDefaultEDBMap());
    Translator translator(context);
    auto sql = translator.DomainToSql(domain);
    if (!sql) return "";
    auto expr = std::static_pointer_cast<sql::ast::Expression>(sql);
    sql::ast::Optimizer optimizer;
    expr = optimizer.Optimize(expr);
    return expr ? expr->ToString() : "";
  }
};

// DomainToSql: direct tests for domain translation (see test_optimization.cc — other translation
// expectations live on OptimizationTest with ValidatingOptimizer).
TEST_F(TranslationTest, DomainToSqlConstantDomain) {
  ConstantDomain domain(42);
  EXPECT_EQ(DomainToSqlString(domain), "SELECT 42 AS A1");
}

TEST_F(TranslationTest, DomainToSqlDefinedDomain) {
  DefinedDomain domain("A", 1);
  EXPECT_EQ(DomainToSqlString(domain), "SELECT T0.A1 AS A1 FROM A AS T0");
}

TEST_F(TranslationTest, DomainToSqlProjection) {
  auto inner = std::make_unique<DefinedDomain>("B", 2);
  Projection proj({0}, std::move(inner));  // project first column only
  EXPECT_EQ(DomainToSqlString(proj), "SELECT T0.A1 AS A1 FROM B AS T0");
}

TEST_F(TranslationTest, DomainToSqlDomainUnion) {
  auto lhs = std::make_unique<DefinedDomain>("A", 1);
  auto rhs = std::make_unique<DefinedDomain>("D", 1);
  DomainUnion domain_union(std::move(lhs), std::move(rhs));
  EXPECT_EQ(DomainToSqlString(domain_union), "SELECT T0.A1 AS A1 FROM A AS T0 UNION SELECT T1.A1 AS A1 FROM D AS T1");
}

TEST_F(TranslationTest, DomainToSqlDomainOperation) {
  auto lhs = std::make_unique<ConstantDomain>(10);
  auto rhs = std::make_unique<ConstantDomain>(2);
  DomainOperation domain_op(std::move(lhs), std::move(rhs), RelTermOp::ADD);
  EXPECT_EQ(DomainToSqlString(domain_op), "SELECT 12 AS A1");
}

}  // namespace rel2sql
