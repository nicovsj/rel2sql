#include <iostream>
#include <sstream>
#include <string>

#include "rel2sql/rel2sql.h"

int main(int argc, char* argv[]) {
  std::ostringstream oss;

  oss << std::cin.rdbuf();

  std::string file_contents = oss.str();

  std::cout << rel2sql::Translate(file_contents) << std::endl;

  return 0;
}
