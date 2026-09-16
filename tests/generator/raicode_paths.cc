#include "generator/raicode_paths.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace rel2sql::generator {
namespace {

std::optional<fs::path> EnvPath(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') return std::nullopt;
  return fs::path(value);
}

bool IsRepoRoot(const fs::path& path) {
  return fs::exists(path / "MODULE.bazel") && fs::exists(path / "tests" / "BUILD");
}

}  // namespace

std::optional<fs::path> FindRepoRoot(const fs::path& start) {
  std::error_code ec;
  fs::path cur = fs::weakly_canonical(start, ec);
  if (ec) cur = fs::absolute(start);

  for (int depth = 0; depth < 12; ++depth) {
    if (IsRepoRoot(cur)) return cur;
    if (!cur.has_parent_path() || cur.parent_path() == cur) break;
    cur = cur.parent_path();
  }
  return std::nullopt;
}

std::filesystem::path RaicodeRoot() {
  if (auto env = EnvPath("REL2SQL_RAICODE_ROOT")) return *env;

  const auto cwd = fs::current_path();
  if (auto root = FindRepoRoot(cwd)) return *root / "third_party" / "raicode";

  if (auto test_srcdir = EnvPath("TEST_SRCDIR")) {
    if (auto root = FindRepoRoot(*test_srcdir)) return *root / "third_party" / "raicode";
    return *test_srcdir / "third_party" / "raicode";
  }

  return cwd / "third_party" / "raicode";
}

std::filesystem::path RelProgramRunnerScript() {
  if (auto env = EnvPath("REL2SQL_REL_RUNNER")) return *env;

  if (auto test_srcdir = EnvPath("TEST_SRCDIR")) {
    const fs::path candidate = *test_srcdir / "_main" / "tests" / "generator" / "run_rel_program.jl";
    if (fs::exists(candidate)) return candidate;
    const fs::path alt = *test_srcdir / "tests" / "generator" / "run_rel_program.jl";
    if (fs::exists(alt)) return alt;
  }

  const auto cwd = fs::current_path();
  if (auto root = FindRepoRoot(cwd)) return *root / "tests" / "generator" / "run_rel_program.jl";
  return cwd / "tests" / "generator" / "run_rel_program.jl";
}

std::string JuliaExecutable() {
  if (const char* julia = std::getenv("REL2SQL_JULIA")) {
    if (julia[0] != '\0') return julia;
  }
  return "julia";
}

bool RaicodeSubmodulePresent() {
  const fs::path project = RaicodeRoot() / "Project.toml";
  std::error_code ec;
  return fs::exists(project, ec);
}

bool JuliaOnPath() {
  const std::string julia = JuliaExecutable();
  if (julia.find('/') != std::string::npos) {
    std::error_code ec;
    return fs::exists(julia, ec);
  }

  for (const char* dir_env : {"PATH"}) {
    const char* path = std::getenv(dir_env);
    if (path == nullptr) continue;
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
      std::error_code ec;
      if (fs::exists(fs::path(dir) / julia, ec)) return true;
    }
  }
  return false;
}

}  // namespace rel2sql::generator
