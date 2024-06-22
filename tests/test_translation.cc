// cspell:ignore GTEST
#include <gtest/gtest.h>

#include "parser/parse.h"
#include "sql.h"

TEST(SQLVisitorTest, EqualitySpecialCondition) {
  std::string input = "F(x) and G(x)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::BinOpContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto table_F = std::make_shared<sql::ast::Table>("F");
  auto table_G = std::make_shared<sql::ast::Table>("G");

  auto visitor = SQLVisitor(ast);

  auto condition = visitor.EqualitySpecialCondition(
      std::unordered_map<antlr4::ParserRuleContext*, std::shared_ptr<sql::ast::Source>>{{tree->lhs, table_F},
                                                                                        {tree->rhs, table_G}});

  std::ostringstream os;

  os << *condition;

  EXPECT_EQ(os.str(), "G.x = F.x");
}

TEST(SQLVisitorTest, SpecialVarList) {
  std::string input = "F(x) and G(x, y)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::BinOpContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto table_F = std::make_shared<sql::ast::Table>("F");
  auto table_G = std::make_shared<sql::ast::Table>("G");

  auto visitor = SQLVisitor(ast);

  auto var_list =
      visitor.SpecialVarList(std::unordered_map<antlr4::ParserRuleContext*, std::shared_ptr<sql::ast::Source>>{
          {tree->lhs, table_F}, {tree->rhs, table_G}});

  std::ostringstream os;

  for (auto& col : var_list) {
    os << *col << " ";
  }

  EXPECT_EQ(os.str(), "G.x G.y ");
}

TEST(TranslationTest, ConjunctionExpr) {
  GTEST_SKIP() << "Not implemented yet";

  std::string input = "F(x) and G(x, y)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::FormulaContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto result = rel_parser::GetSQLFromTree(tree);

  std::ostringstream os;

  os << *result;

  EXPECT_EQ(os.str(), "SELECT F.x FROM F WHERE G.x = F.x");
}
