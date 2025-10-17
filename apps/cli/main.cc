#include <rel2sql/rel2sql.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char* argv[]) {
  std::ostringstream oss;

  std::istream* input = &std::cin;
  std::ifstream file;
  bool unoptimized = false;
  std::string filename;

  // Parse arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-u") {
      unoptimized = true;
    } else if (arg == "-f") {
      if (i + 1 >= argc) {
        std::cerr << "Error: -f requires a filename argument" << std::endl;
        return 1;
      }
      filename = argv[++i];
    } else {
      std::cerr << "Usage: " << argv[0] << " [-f filename] [-u]" << std::endl;
      return 1;
    }
  }

  // Handle file input if specified
  if (!filename.empty()) {
    file.open(filename);
    if (!file.is_open()) {
      std::cerr << "Error: Could not open file " << filename << std::endl;
      return 1;
    }
    input = &file;
  }

  oss << input->rdbuf();

  std::string file_contents = oss.str();

  if (unoptimized) {
    std::cout << rel2sql::DumbTranslate(file_contents) << std::endl;
  } else {
    std::cout << rel2sql::Translate(file_contents) << std::endl;
  }

  if (file.is_open()) {
    file.close();
  }

  return 0;
}
