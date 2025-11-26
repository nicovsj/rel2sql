#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#endif

#include <string>
#include <string_view>
#include <vector>

// Include the actual rel2sql headers
#include "rel2sql/rel2sql.h"
#include "rel_ast/relation_info.h"

// WebAssembly wrapper functions
std::string translate_rel2sql(const std::string& input) { return rel2sql::Translate(input); }

// Wrapper for Translate with EDB map (use exact alias type to match embind)
std::string translate_rel2sql_with_relation_map(const std::string& input, const rel2sql::RelationMap& relation_map) {
  return rel2sql::Translate(input, relation_map);
}

// Test function to verify the library is working
bool test_rel2sql() {
  std::string result = rel2sql::Translate("def output {1}");
  return !result.empty();
}

// Test function for EDB-based translation
bool test_rel2sql_with_relation_map() {
  rel2sql::RelationMap relation_map;
  relation_map["relation1"] = rel2sql::RelationInfo(2);                                        // Create EDB with arity 2
  relation_map["relation2"] = rel2sql::RelationInfo(std::vector<std::string>{"name", "age"});  // Create EDB with named attributes

  std::string result = rel2sql::Translate("def output {1}", relation_map);
  return !result.empty();
}

// Emscripten bindings
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(rel2sql_module) {
  // Register std::vector<std::string> as StringVector for JavaScript
  emscripten::register_vector<std::string>("StringVector");

  // Expose EDBInfo struct to JavaScript - only with std::vector<std::string> constructor
  emscripten::class_<rel2sql::RelationInfo>("RelationInfo")
      .constructor<std::vector<std::string>>()
      .function("arity", &rel2sql::RelationInfo::Arity)
      .function("hasCustomNamedAttributes", &rel2sql::RelationInfo::HasCustomNamedAttributes)
      .function("getAttributeName", &rel2sql::RelationInfo::AttributeName);

  // Expose EDBMap to JavaScript
  emscripten::class_<rel2sql::RelationMap>("RelationMap")
      .constructor<>()
      .function("size", &rel2sql::RelationMap::size)
      .function("has", &rel2sql::RelationMap::has)
      .function("get", &rel2sql::RelationMap::get)
      .function("set", &rel2sql::RelationMap::set);

  // Expose functions
  emscripten::function("translate", &translate_rel2sql);
  emscripten::function("translateWithRelationMap", &translate_rel2sql_with_relation_map);
  emscripten::function("test", &test_rel2sql);
  emscripten::function("testWithRelationMap", &test_rel2sql_with_relation_map);
}
#endif
