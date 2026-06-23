#ifndef REL2SQL_TESTS_GENERATOR_REL_ENGINE_CLIENT_H_
#define REL2SQL_TESTS_GENERATOR_REL_ENGINE_CLIENT_H_

#include <optional>
#include <string>

#include "generator/data_fixture.h"
#include "generator/result_set.h"

namespace rel2sql::generator {

class RelEngineClient {
 public:
  static std::optional<std::string> DefaultSocketPath();

  explicit RelEngineClient(std::string socket_path);

  bool Connect();
  void Disconnect();

  bool Ping();

  ResultSet Run(const std::string& rel_program, const DataFixture& fixture, const std::string& output_def,
                int output_arity);

  bool IsConnected() const { return fd_ >= 0; }

  const std::string& SocketPath() const { return socket_path_; }

 private:
  std::string SendRequest(const std::string& json_line);

  std::string socket_path_;
  int fd_ = -1;
};

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_REL_ENGINE_CLIENT_H_
