#include "generator/rel_engine_client.h"

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "generator/raicode_paths.h"
#include "generator/rel_engine_json.h"

namespace rel2sql::generator {
namespace {

constexpr int kRelEngineRecvTimeoutSec = 240;

void SetSocketTimeout(int fd) {
  timeval tv{};
  tv.tv_sec = kRelEngineRecvTimeoutSec;
  tv.tv_usec = 0;
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
    throw std::runtime_error(std::string("rel engine setsockopt SO_RCVTIMEO failed: ") + std::strerror(errno));
  }
}

}  // namespace

std::optional<std::string> RelEngineClient::DefaultSocketPath() {
  if (const char* env = std::getenv("REL2SQL_REL_ENGINE_SOCKET")) {
    if (env[0] != '\0') return std::string(env);
  }
  if (auto root = FindRepoRoot(std::filesystem::current_path())) {
    return (root.value() / ".rel_engine.sock").string();
  }
  return std::nullopt;
}

RelEngineClient::RelEngineClient(std::string socket_path) : socket_path_(std::move(socket_path)) {}

void RelEngineClient::Disconnect() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

bool RelEngineClient::Connect() {
  Disconnect();

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (socket_path_.size() >= sizeof(addr.sun_path)) {
    return false;
  }
  std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

  fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd_ < 0) return false;

  if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    Disconnect();
    return false;
  }
  SetSocketTimeout(fd_);
  return true;
}

std::string RelEngineClient::SendRequest(const std::string& json_line) {
  if (fd_ < 0) throw std::runtime_error("rel engine client not connected");

  std::string payload = json_line;
  payload.push_back('\n');

  size_t written = 0;
  while (written < payload.size()) {
    const ssize_t n = write(fd_, payload.data() + written, payload.size() - written);
    if (n <= 0) {
      throw std::runtime_error(std::string("rel engine write failed: ") + std::strerror(errno));
    }
    written += static_cast<size_t>(n);
  }

  std::string response;
  char buf[4096];
  while (response.find('\n') == std::string::npos) {
    const ssize_t n = read(fd_, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        Disconnect();
        throw std::runtime_error("rel engine read timed out after " + std::to_string(kRelEngineRecvTimeoutSec) + "s");
      }
      Disconnect();
      throw std::runtime_error(std::string("rel engine read failed: ") + std::strerror(errno));
    }
    if (n == 0) {
      Disconnect();
      throw std::runtime_error("rel engine connection closed before response");
    }
    response.append(buf, static_cast<size_t>(n));
  }

  const size_t nl = response.find('\n');
  return response.substr(0, nl);
}

bool RelEngineClient::Ping() {
  if (!IsConnected() && !Connect()) return false;
  try {
    const std::string response = SendRequest("{\"op\":\"ping\"}");
    return response.find("\"ok\":true") != std::string::npos || response.find("\"ok\": true") != std::string::npos;
  } catch (...) {
    return false;
  }
}

ResultSet RelEngineClient::Run(const std::string& rel_program, const DataFixture& fixture,
                               const std::string& output_def, int output_arity) {
  if (!IsConnected() && !Connect()) {
    throw std::runtime_error("failed to connect to rel engine server at " + socket_path_);
  }

  const std::string request = BuildRelEngineRunRequestLine(rel_program, output_def, fixture, output_arity);
  const std::string response = SendRequest(request);
  return ParseRelEngineResponseJson(response);
}

}  // namespace rel2sql::generator
