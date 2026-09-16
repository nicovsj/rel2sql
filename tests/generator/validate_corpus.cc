#include <fstream>
#include <iostream>

#include "generator/corpus.h"

int main() {
  using namespace rel2sql::generator;
  try {
    const auto root = CorpusV1Root();
    std::cout << "root: " << root << "\n";
    const auto manifest = LoadManifest(root / "manifest.json");
    std::cout << "manifest shards: " << manifest.shards.size() << " cases: " << manifest.case_count << "\n";
    for (const auto& shard_name : manifest.shards) {
      if (!shard_name.starts_with("shard_")) continue;
      const auto path = root / shard_name;
      std::ifstream in(path);
      std::string line;
      size_t line_no = 0;
      while (std::getline(in, line)) {
        if (line.empty()) continue;
        ++line_no;
        try {
          (void)ParseCorpusCaseLine(line);
        } catch (const std::exception& e) {
          std::cerr << shard_name << ":" << line_no << ": " << e.what() << "\n";
          return 1;
        }
      }
      std::cout << shard_name << " ok (" << line_no << " lines)\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
