#ifndef REL2SQL_TESTS_GENERATOR_CORPUS_H_
#define REL2SQL_TESTS_GENERATOR_CORPUS_H_

#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "generator/data_fixture.h"
#include "generator/rel_program_generator.h"
#include "generator/result_set.h"

namespace rel2sql::generator {

struct CorpusCase {
  std::string id;
  SuiteConfig config;
  std::string program;
  std::string output_def;
  RelationMap schema;
  std::unordered_map<std::string, TableRows> edb;
  ResultSet expected;
};

struct CorpusManifest {
  int corpus_version = 0;
  std::string generator_fingerprint;
  std::string edb_map;
  size_t case_count = 0;
  std::vector<std::string> shards;
};

std::filesystem::path CorpusV1Root();
std::filesystem::path CorpusManifestPath();

CorpusManifest LoadManifest(const std::filesystem::path& manifest_path);
std::vector<CorpusCase> LoadShard(const std::filesystem::path& shard_path);
std::vector<std::string> ListShardPaths(const CorpusManifest& manifest, const std::filesystem::path& corpus_root);

std::string SerializeCorpusCaseLine(const CorpusCase& corpus_case);
CorpusCase ParseCorpusCaseLine(const std::string& line);

DataFixture DataFixtureFromCorpus(const CorpusCase& corpus_case);

// Program texts already present in corpus shards (for build-time deduplication).
std::set<std::string> LoadExistingPrograms(const std::filesystem::path& corpus_root);

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_CORPUS_H_
