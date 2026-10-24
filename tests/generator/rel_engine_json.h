#ifndef REL2SQL_TESTS_GENERATOR_REL_ENGINE_JSON_H_
#define REL2SQL_TESTS_GENERATOR_REL_ENGINE_JSON_H_

#include <string>

#include "generator/data_fixture.h"
#include "generator/result_set.h"

namespace rel2sql::generator {

std::string JsonEscape(std::string_view text);

std::string BuildRelEngineRequestJson(const std::string& program, const std::string& output_def,
                                      const DataFixture& fixture, int output_arity);

// Compact single-line JSON for socket protocol.
std::string BuildRelEngineRunRequestLine(const std::string& program, const std::string& output_def,
                                         const DataFixture& fixture, int output_arity);

ResultSet ParseRelEngineResponseJson(const std::string& json);

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_REL_ENGINE_JSON_H_
