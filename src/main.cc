#include <fstream>

#include "RelLexer.h"
#include "RelParser.h"
#include "antlr4-runtime.h"

int main(int argc, const char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <input_file>" << std::endl;
    return 1;
  }

  std::string input_str = argv[1];

  std::ifstream input_file(input_str);

  antlr4::ANTLRInputStream input(input_file);

  rel_parser::RelLexer lexer(&input);
  antlr4::CommonTokenStream tokens(&lexer);
  rel_parser::RelParser parser(&tokens);

  rel_parser::RelParser::ProgContext *tree = parser.prog();

  // Print the AST
  std::cout << tree->toStringTree(&parser, true) << std::endl;

  return 0;
}