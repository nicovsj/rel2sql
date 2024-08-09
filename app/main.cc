#include <iostream>
#include <sstream>
#include <string>

#include "rel2sql/rel2sql.h"

int main(int argc, char* argv[]) {
  std::ostringstream oss;

  std::istream* input = &std::cin;
  std::ifstream file;

  if (argc > 2 && std::string(argv[1]) == "-f") {
    file.open(argv[2]);
    if (!file.is_open()) {
      std::cerr << "Error: Could not open file " << argv[2] << std::endl;
      return 1;
    }
    input = &file;
  } else if (argc > 1) {
    std::cerr << "Usage: " << argv[0] << " [-f filename]" << std::endl;
    return 1;
  }

  oss << input->rdbuf();

  std::string file_contents = oss.str();

  std::cout << rel2sql::Translate(file_contents) << std::endl;

  if (file.is_open()) {
    file.close();
  }

  return 0;
}
