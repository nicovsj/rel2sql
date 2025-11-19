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
#include "rel_ast/edb_info.h"

// WebAssembly wrapper functions
std::string translate_rel2sql(const std::string& input) { return rel2sql::Translate(input); }

// Wrapper for Translate with EDB map (use exact alias type to match embind)
std::string translate_rel2sql_with_rel_map(const std::string& input, const rel2sql::RelationMap& edb_map) {
  return rel2sql::Translate(input, edb_map);
}

// Test function to verify the library is working
bool test_rel2sql() {
  std::string result = rel2sql::Translate("def output {1}");
  return !result.empty();
}

// Test function for EDB-based translation
bool test_rel2sql_with_edb() {
  std::unordered_map<std::string, rel2sql::RelationInfo> edb_map;
  edb_map["relation1"] = rel2sql::RelationInfo(2);                                        // Create EDB with arity 2
  edb_map["relation2"] = rel2sql::RelationInfo(std::vector<std::string>{"name", "age"});  // Create EDB with named attributes

  std::string result = rel2sql::Translate("def output {1}", edb_map);
  return !result.empty();
}

// Factory function for creating EDBInfo with arity
rel2sql::RelationInfo createEDBInfoWithArity(int arity) { return rel2sql::RelationInfo(arity); }

// Factory function to create EDBInfo with names using std::vector<std::string>
rel2sql::RelationInfo createEDBInfoWithNames(const std::vector<std::string>& names) { return rel2sql::RelationInfo(names); }

// Emscripten bindings
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(rel2sql_module) {
  // Register std::vector<std::string> as StringVector for JavaScript
  emscripten::register_vector<std::string>("StringVector");

  // Expose EDBInfo struct to JavaScript - only with std::vector<std::string> constructor
  emscripten::class_<rel2sql::RelationInfo>("RelationInfo")
      .constructor<std::vector<std::string>>()
      .function("arity", &rel2sql::RelationInfo::arity)
      .function("hasCustomNamedAttributes", &rel2sql::RelationInfo::has_custom_named_attributes)
      .function("getAttributeName", &rel2sql::RelationInfo::get_attribute_name);

  // Expose EDBMap to JavaScript
  emscripten::class_<rel2sql::RelationMap>("RelationMap")
      .constructor<>()
      .function("size", &rel2sql::RelationMap::size)
      .function("has", &rel2sql::RelationMap::has)
      .function("get", &rel2sql::RelationMap::get)
      .function("set", &rel2sql::RelationMap::set);

  // Expose functions
  emscripten::function("translate", &translate_rel2sql);
  emscripten::function("translateWithEDB", &translate_rel2sql_with_edb);
  emscripten::function("test", &test_rel2sql);
  emscripten::function("testWithEDB", &test_rel2sql_with_edb);
}
#endif
