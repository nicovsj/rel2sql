#include <string>
#include <vector>

#include <antlr4-runtime.h>


int main(int argc, const char *argv[]) {
  std::vector<std::string> msg{"Hello", "C++",     "World",
                               "from",  "VS Code", "and the C++ extension!"};
  
  std::ifstream stream;

  stream.open("example.txt");

  antlr4::ANTLRInputStream input(stream);

  return 0;
}