#include <gtest/gtest.h>

#include "parser/parse.h"
#include "sql.h"

TEST(TranslationUtilityFunctionsTest, EqualitySS) {
  std::string input = "F(x) and G(x)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::BinOpContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto table_F = std::make_shared<Table>("F");
  auto table_G = std::make_shared<Table>("G");

  auto condition =
      EqualitySS(std::unordered_map<antlr4::ParserRuleContext*, std::shared_ptr<Source>>{{tree->lhs, table_F},
                                                                                         {tree->rhs, table_G}},
                 *ast.extended_data);

  std::ostringstream os;

  os << *condition;

  EXPECT_EQ(os.str(), "G.x = F.x");
}

TEST(TranslationUtilityFunctionsTest, VarListSS) {
  std::string input = "F(x) and G(x, y)";

  auto parser = rel_parser::GetParser(input);

  auto tree = dynamic_cast<rel_parser::PrunedCoreRelParser::BinOpContext*>(parser->formula());

  auto ast = rel_parser::GetExtendedASTFromTree(tree);

  auto table_F = std::make_shared<Table>("F");
  auto table_G = std::make_shared<Table>("G");

  auto var_list =
      VarListSS(std::unordered_map<antlr4::ParserRuleContext*, std::shared_ptr<Source>>{{tree->lhs, table_F},
                                                                                        {tree->rhs, table_G}},
                *ast.extended_data);

  std::ostringstream os;

  for (auto& col : var_list) {
    os << *col << " ";
  }

  EXPECT_EQ(os.str(), "G.x G.y ");
}
