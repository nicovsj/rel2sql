#include "generator/rel_engine.h"

#include <cstdlib>

namespace rel2sql::generator {

bool RelEngine::IsAvailable() {
  const char* path = std::getenv("REL2SQL_REL_ENGINE");
  return path != nullptr && path[0] != '\0';
}

ResultSet RelEngine::Run(const std::string& /*rel_program*/, const DataFixture& /*fixture*/,
                         const std::string& /*output_def*/) {
  // Stub until a Rel execution backend is wired in.
  return ResultSet{};
}

}  // namespace rel2sql::generator
