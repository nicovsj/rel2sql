#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#endif

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Include the actual rel2sql headers
#include "rel2sql/rel2sql.h"
#include "structs/edb_info.h"

// WebAssembly wrapper functions
std::string translate_rel2sql(const std::string& input) { return rel2sql::Translate(input); }

// Wrapper for Translate with EDB map (use exact alias type to match embind)
std::string translate_rel2sql_with_edb(const std::string& input, const rel2sql::EDBMap& edb_map) {
  return rel2sql::Translate(input, edb_map);
}

// Test function to verify the library is working
bool test_rel2sql() {
  std::string result = rel2sql::Translate("def output {1}");
  return !result.empty();
}

// Test function for EDB-based translation
bool test_rel2sql_with_edb() {
  std::unordered_map<std::string, rel2sql::EDBInfo> edb_map;
  edb_map["relation1"] = rel2sql::EDBInfo(2);                                        // Create EDB with arity 2
  edb_map["relation2"] = rel2sql::EDBInfo(std::vector<std::string>{"name", "age"});  // Create EDB with named attributes

  std::string result = rel2sql::Translate("def output {1}", edb_map);
  return !result.empty();
}

// Factory function for creating EDBInfo with arity
rel2sql::EDBInfo createEDBInfoWithArity(int arity) { return rel2sql::EDBInfo(arity); }

// Factory function to create EDBInfo with names using std::vector<std::string>
rel2sql::EDBInfo createEDBInfoWithNames(const std::vector<std::string>& names) { return rel2sql::EDBInfo(names); }

// Emscripten bindings
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(rel2sql_module) {
  // Register std::vector<std::string> as StringVector for JavaScript
  emscripten::register_vector<std::string>("StringVector");

  // Expose EDBInfo struct to JavaScript - only with std::vector<std::string> constructor
  emscripten::class_<rel2sql::EDBInfo>("EDBInfo")
      .constructor<std::vector<std::string>>()
      .function("arity", &rel2sql::EDBInfo::arity)
      .function("hasCustomNamedAttributes", &rel2sql::EDBInfo::has_custom_named_attributes)
      .function("getAttributeName", &rel2sql::EDBInfo::get_attribute_name);

  // Expose EDBMap to JavaScript
  emscripten::class_<rel2sql::EDBMap>("EDBMap")
      .constructor<>()
      .function("size", &rel2sql::EDBMap::size)
      .function("has", &rel2sql::EDBMap::has)
      .function("get", &rel2sql::EDBMap::get)
      .function("set", &rel2sql::EDBMap::set);

  // Expose functions
  emscripten::function("translate", &translate_rel2sql);
  emscripten::function("translateWithEDB", &translate_rel2sql_with_edb);
  emscripten::function("test", &test_rel2sql);
  emscripten::function("testWithEDB", &test_rel2sql_with_edb);
}
#endif
