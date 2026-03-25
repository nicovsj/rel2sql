#include <gtest/gtest.h>

#include "api/translate.h"
#include "preprocessing/arity_visitor.h"
#include "preprocessing/ids_visitor.h"
#include "preprocessing/vars_visitor.h"
#include "rel_ast/rel_ast_builder.h"
#include "rel_ast/rel_ast_visitor.h"
#include "rel_ast/rel_context_builder.h"
#include "rel_ast/relation_info.h"

namespace rel2sql {

TEST(RelASTBuilderTest, RelASTStructure) {
  // Manually create a minimal RelProgram to verify the structure
  auto literal = std::make_shared<RelLiteral>(RelLiteralValue(1));
  std::vector<std::shared_ptr<RelExpr>> exprs;
  exprs.push_back(literal);
  auto rel_abs = std::make_shared<RelUnion>(exprs);
  auto def = std::make_shared<RelDef>("output", rel_abs);
  std::vector<std::shared_ptr<RelDef>> defs;
  defs.push_back(def);
  auto program = std::make_shared<RelProgram>(defs);

  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->defs.size(), 1u);
  EXPECT_EQ(program->defs[0]->name, "output");
}

TEST(RelASTBuilderTest, BuildSimpleProgram) {
  auto parser = GetParser("def output { (1, 2) }");
  auto tree = parser->program();
  ASSERT_NE(tree, nullptr);

  RelASTBuilder builder;
  auto program = builder.Build(tree);

  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->defs.size(), 1u);
  EXPECT_EQ(program->defs[0]->name, "output");
  ASSERT_NE(program->defs[0]->body, nullptr);
  EXPECT_EQ(program->defs[0]->body->exprs.size(), 1u);
}

TEST(RelASTBuilderTest, IDsVisitorPopulatesContainer) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);
  edb_map["B"] = RelationInfo(1);

  auto parser = GetParser("def R { A(x) where B(x) }");
  auto tree = parser->program();

  RelASTBuilder ast_builder;
  auto program = ast_builder.Build(tree);

  RelContextBuilder context_builder(edb_map);
  context_builder.SetRoot(program);

  IDsVisitor ids_visitor(&context_builder);
  ids_visitor.Visit(program);

  EXPECT_TRUE(context_builder.IsIDB("R"));
  EXPECT_TRUE(context_builder.IsEDB("A"));
  EXPECT_TRUE(context_builder.IsEDB("B"));
  EXPECT_EQ(context_builder.SortedIDs().size(), 3u);  // R, A, B
}

TEST(RelASTBuilderTest, ArityVisitorPopulatesArity) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(2);  // A has arity 2
  edb_map["B"] = RelationInfo(1);

  auto parser = GetParser("def R { A(x, y) where B(x) }");
  auto tree = parser->program();

  RelASTBuilder ast_builder;
  auto program = ast_builder.Build(tree);

  RelContextBuilder context_builder(edb_map);
  context_builder.SetRoot(program);

  IDsVisitor ids_visitor(&context_builder);
  ids_visitor.Visit(program);

  ArityVisitor arity_visitor(&context_builder);
  arity_visitor.Visit(program);

  EXPECT_EQ(context_builder.GetArity("R"), 0);  // Formula expr has arity 0
  EXPECT_EQ(context_builder.GetArity("A"), 2);
  EXPECT_EQ(context_builder.GetArity("B"), 1);
}

TEST(RelASTBuilderTest, VariablesVisitorPopulatesVars) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);
  edb_map["B"] = RelationInfo(1);

  auto parser = GetParser("def R { A(x) where B(x) }");
  auto tree = parser->program();

  RelASTBuilder ast_builder;
  auto program = ast_builder.Build(tree);

  RelContextBuilder context_builder(edb_map);
  context_builder.SetRoot(program);

  IDsVisitor ids_visitor(&context_builder);
  ids_visitor.Visit(program);

  ArityVisitor arity_visitor(&context_builder);
  arity_visitor.Visit(program);

  VariablesVisitor vars_visitor(&context_builder);
  vars_visitor.Visit(program);

  // R's body is condition: A(x) where B(x). Variables should include x.
  ASSERT_NE(program->defs[0]->body, nullptr);
  EXPECT_TRUE(program->defs[0]->body->variables.count("x") > 0);
  EXPECT_TRUE(program->defs[0]->body->free_variables.count("x") > 0);
}

TEST(RelASTBuilderTest, RelContextBuilderPipeline) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);
  edb_map["B"] = RelationInfo(1);

  auto parser = GetParser("def R { A(x) where B(x) }");
  RelASTBuilder ast_builder;
  auto program = ast_builder.Build(parser->program());

  RelContextBuilder builder(edb_map);
  auto context = builder.Process(program);

  EXPECT_TRUE(context.IsIDB("R"));
  EXPECT_TRUE(context.IsEDB("A"));
  EXPECT_TRUE(context.IsEDB("B"));
  EXPECT_EQ(context.GetArity("R"), 0);
  EXPECT_EQ(context.GetArity("A"), 1);
  EXPECT_EQ(context.GetArity("B"), 1);

  auto root = context.Root();
  ASSERT_NE(root, nullptr);
  auto* program_node = dynamic_cast<RelProgram*>(root.get());
  ASSERT_NE(program_node, nullptr);
  ASSERT_NE(program_node->defs[0]->body, nullptr);
  EXPECT_TRUE(program_node->defs[0]->body->variables.count("x") > 0);
}

TEST(RelASTBuilderTest, SQLVisitorRelLiteralProgram) {
  // Full Rel pipeline: parse -> RelASTBuilder -> RelContextBuilder -> GetSQLFromRelContainer
  RelationMap edb_map;
  auto parser = GetParser("def output { (1, 2) }");
  RelASTBuilder ast_builder;
  auto program = ast_builder.Build(parser->program());

  RelContextBuilder builder(edb_map);
  auto context = builder.Process(program);

  auto sql = GetSQLFromRelContext(context);
  ASSERT_NE(sql, nullptr);
  std::string sql_str = sql->ToString();
  // Expected: SELECT DISTINCT 1 AS A1, 2 AS A2 (or similar)
  EXPECT_TRUE(sql_str.find("1") != std::string::npos);
  EXPECT_TRUE(sql_str.find("2") != std::string::npos);
  EXPECT_TRUE(sql_str.find("SELECT") != std::string::npos);
}

TEST(RelASTBuilderTest, SQLVisitorRelEDBProgram) {
  // Rel pipeline with EDB: def output { A }
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(2);  // A(x, y)
  auto parser = GetParser("def output { A }");
  RelASTBuilder ast_builder;
  auto program = ast_builder.Build(parser->program());

  RelContextBuilder builder(edb_map);
  auto context = builder.Process(program);

  auto sql = GetSQLFromRelContext(context);
  ASSERT_NE(sql, nullptr);
  std::string sql_str = sql->ToString();
  EXPECT_TRUE(sql_str.find("SELECT") != std::string::npos);
  EXPECT_TRUE(sql_str.find("A") != std::string::npos);
}

TEST(RelASTBuilderTest, GetUnoptimizedSQLRel) {
  RelationMap edb_map;
  auto sql = GetUnoptimizedSQLRel("def output { (1, 2) }", edb_map);
  ASSERT_NE(sql, nullptr);
  std::string sql_str = sql->ToString();
  EXPECT_TRUE(sql_str.find("1") != std::string::npos);
  EXPECT_TRUE(sql_str.find("2") != std::string::npos);
}

TEST(RelASTBuilderTest, GetSQLRel) {
  RelationMap edb_map;
  auto sql = GetSQLRel("def output { (1, 2) }", edb_map);
  ASSERT_NE(sql, nullptr);
  std::string sql_str = sql->ToString();
  EXPECT_TRUE(sql_str.find("1") != std::string::npos);
  EXPECT_TRUE(sql_str.find("2") != std::string::npos);
}

TEST(RelASTBuilderTest, SQLVisitorRelFullApplication) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);
  edb_map["B"] = RelationInfo(2);
  edb_map["C"] = RelationInfo(3);
  EXPECT_EQ(GetUnoptimizedSQLRel("def output { A(x) }", edb_map)->ToString(),
            "SELECT DISTINCT T0.A1 AS x FROM A AS T0;");
  EXPECT_EQ(GetUnoptimizedSQLRel("def output { B(x, y) }", edb_map)->ToString(),
            "SELECT DISTINCT T0.A1 AS x, T0.A2 AS y FROM B AS T0;");
  EXPECT_EQ(GetUnoptimizedSQLRel("def output { B(x, x) }", edb_map)->ToString(),
            "SELECT DISTINCT T0.A1 AS x FROM B AS T0 WHERE T0.A1 = T0.A2;");
}

TEST(RelASTBuilderTest, SQLVisitorRelConditionExpr) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);
  edb_map["B"] = RelationInfo(1);
  auto sql = GetUnoptimizedSQLRel("def output { A(x) where B(x) }", edb_map);
  ASSERT_NE(sql, nullptr);
  std::string s = sql->ToString();
  EXPECT_TRUE(s.find("SELECT") != std::string::npos);
  EXPECT_TRUE(s.find("A") != std::string::npos);
  EXPECT_TRUE(s.find("B") != std::string::npos);
  EXPECT_TRUE(s.find("WHERE") != std::string::npos);
}

TEST(RelASTBuilderTest, DISABLED_SQLVisitorRelConjunctionWithTerms) {
  RelationMap edb_map;
  edb_map["A"] = RelationInfo(1);
  edb_map["B"] = RelationInfo(2);
  auto sql = GetUnoptimizedSQLRel("def output { A(x) and x > 5 }", edb_map);
  ASSERT_NE(sql, nullptr);
  std::string s = sql->ToString();
  EXPECT_TRUE(s.find("SELECT") != std::string::npos);
  EXPECT_TRUE(s.find("WHERE") != std::string::npos);
  EXPECT_TRUE(s.find("5") != std::string::npos);
  sql = GetUnoptimizedSQLRel("def output { B(x,y) and x + y > 5 }", edb_map);
  ASSERT_NE(sql, nullptr);
  s = sql->ToString();
  EXPECT_TRUE(s.find("WHERE") != std::string::npos);
}

}  // namespace rel2sql
