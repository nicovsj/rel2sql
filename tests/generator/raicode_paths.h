#ifndef REL2SQL_TESTS_GENERATOR_RAICODE_PATHS_H_
#define REL2SQL_TESTS_GENERATOR_RAICODE_PATHS_H_

#include <filesystem>
#include <optional>
#include <string>

namespace rel2sql::generator {

// Walks upward from `start` looking for MODULE.bazel (repo root).
std::optional<std::filesystem::path> FindRepoRoot(const std::filesystem::path& start);

std::filesystem::path RaicodeRoot();
std::filesystem::path RelProgramRunnerScript();

std::string JuliaExecutable();

bool RaicodeSubmodulePresent();
bool JuliaOnPath();

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_RAICODE_PATHS_H_
