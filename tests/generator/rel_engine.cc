#include "generator/rel_engine.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "generator/raicode_paths.h"
#include "generator/rel_engine_client.h"
#include "generator/rel_engine_json.h"

namespace rel2sql::generator {
namespace {

std::mutex g_client_mutex;
std::unique_ptr<RelEngineClient> g_client;

int OutputArity(const DataFixture& fixture, const std::string& output_def, const RelationMap* output_schema) {
  const auto& map = output_schema != nullptr ? output_schema->map : fixture.Schema().map;
  return map.at(output_def).arity;
}

bool EnvEnabled(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

std::string ShellQuote(const std::string& text) {
  std::ostringstream os;
  os << '"';
  for (char c : text) {
    if (c == '"')
      os << "\\\"";
    else if (c == '\\')
      os << "\\\\";
    else if (c == '$')
      os << "\\$";
    else
      os << c;
  }
  os << '"';
  return os.str();
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("failed to read file: " + path.string());
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

std::filesystem::path WriteTempRequest(const std::string& json) {
  const auto path = std::filesystem::temp_directory_path() / "rel2sql_rel_engine_request.json";
  std::ofstream out(path);
  if (!out) throw std::runtime_error("failed to write request json");
  out << json;
  out.close();
  return path;
}

RelEngineClient* TryGetClient() {
  std::lock_guard<std::mutex> lock(g_client_mutex);
  if (g_client && g_client->IsConnected() && g_client->Ping()) {
    return g_client.get();
  }

  auto socket = RelEngineClient::DefaultSocketPath();
  if (!socket) return nullptr;

  auto candidate = std::make_unique<RelEngineClient>(*socket);
  if (!candidate->Ping()) return nullptr;

  g_client = std::move(candidate);
  return g_client.get();
}

ResultSet RunSubprocess(const std::string& rel_program, const DataFixture& fixture, const std::string& output_def,
                        const RelationMap* output_schema) {
  const int output_arity = OutputArity(fixture, output_def, output_schema);
  const auto request_path = WriteTempRequest(BuildRelEngineRequestJson(rel_program, output_def, fixture, output_arity));
  const auto response_path = std::filesystem::temp_directory_path() / "rel2sql_rel_engine_response.json";

  const std::filesystem::path julia_project = RaicodeRoot();
  const std::filesystem::path runner = RelProgramRunnerScript();
  if (!std::filesystem::exists(runner)) {
    throw std::runtime_error("Julia runner script not found: " + runner.string());
  }

  std::ostringstream cmd;
  cmd << "JULIA_PROJECT=" << ShellQuote(julia_project.string()) << " ";
  cmd << ShellQuote(JuliaExecutable()) << " ";
  cmd << ShellQuote(runner.string()) << " ";
  cmd << ShellQuote(request_path.string());
  cmd << " > " << ShellQuote(response_path.string()) << " 2> "
      << ShellQuote((std::filesystem::temp_directory_path() / "rel2sql_rel_engine_stderr.txt").string());

  const int rc = std::system(cmd.str().c_str());
  if (rc != 0) {
    std::ostringstream err;
    err << "RAICode runner failed with exit code " << rc;
    const auto stderr_path = std::filesystem::temp_directory_path() / "rel2sql_rel_engine_stderr.txt";
    if (std::filesystem::exists(stderr_path)) {
      err << "\n" << ReadFile(stderr_path);
    }
    throw std::runtime_error(err.str());
  }

  return ParseRelEngineResponseJson(ReadFile(response_path));
}

}  // namespace

bool RelEngine::IsAvailable() {
  if (!EnvEnabled("REL2SQL_ENABLE_RAICODE") && !EnvEnabled("REL2SQL_REL_ENGINE")) {
    return false;
  }
  return RaicodeSubmodulePresent() && JuliaOnPath();
}

bool RelEngine::IsServerReachable() { return TryGetClient() != nullptr; }

ResultSet RelEngine::Run(const std::string& rel_program, const DataFixture& fixture, const std::string& output_def,
                         const RelationMap* output_schema) {
  if (!IsAvailable()) {
    throw std::runtime_error("RAICode RelEngine is not available");
  }

  if (RelEngineClient* client = TryGetClient()) {
    try {
      return client->Run(rel_program, fixture, output_def, OutputArity(fixture, output_def, output_schema));
    } catch (const std::runtime_error& ex) {
      const bool app_error = std::string_view(ex.what()).starts_with("RAICode runner error:");
      if (!app_error) {
        std::lock_guard<std::mutex> lock(g_client_mutex);
        g_client.reset();
      }
      throw;
    } catch (...) {
      std::lock_guard<std::mutex> lock(g_client_mutex);
      g_client.reset();
      throw;
    }
  }

  if (RelEngineClient::DefaultSocketPath()) {
    throw std::runtime_error("rel engine server not reachable (start with: task rel-engine:start)");
  }

  std::cerr << "[RelEngine] no warm server; one-shot Julia (expect several minutes)...\n" << std::flush;
  return RunSubprocess(rel_program, fixture, output_def, output_schema);
}

}  // namespace rel2sql::generator
