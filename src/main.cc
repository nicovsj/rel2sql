#include <fstream>

#include "antlr4-runtime.h"
#include "RelLexer.h"
#include "RelParser.h"

int main(int argc, const char *argv[]) {
  std::ifstream stream;

  stream.open("example.txt");

  antlr4::ANTLRInputStream input(stream);

  return 0;
}