#include <antlr4-runtime.h>

#include <fstream>

#include "parser/generated/CoreRelLexer.h"
#include "parser/generated/PrunedCoreRelParser.h"
#include "src/parser/fv_visitor.h"

int main(int argc, const char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <input_file>" << std::endl;
    return 1;
  }

  std::string input_str = argv[1];

  std::ifstream input_file(input_str);

  antlr4::ANTLRInputStream input(input_file);

  rel_parser::CoreRelLexer lexer(&input);
  antlr4::CommonTokenStream tokens(&lexer);
  rel_parser::PrunedCoreRelParser parser(&tokens);

  rel_parser::PrunedCoreRelParser::ProgramContext *tree = parser.program();

  ExtendedASTVisitor visitor;

  auto free_vars = std::any_cast<std::set<std::string>>(visitor.visit(tree));

  // Print the AST
  for (auto &var : free_vars) {
    std::cout << var << std::endl;
  }

  return 0;
}
