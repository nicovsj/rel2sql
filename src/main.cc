#include <antlr4-runtime.h>

#include <fstream>

#include "parser/generated/CoreRelLexer.h"
#include "parser/generated/CoreRelParser.h"

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
  rel_parser::CoreRelParser parser(&tokens);

  rel_parser::CoreRelParser::ProgramContext *tree = parser.program();

  // Print the AST
  std::cout << tree->toStringTree(&parser, true) << std::endl;

  return 0;
}
