#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#endif

#include <string>
#include <string_view>

// Forward declaration - we'll link against the rel2sql library
namespace rel2sql {

std::string Translate(std::string_view input);

}

// WebAssembly wrapper functions
std::string translate_rel2sql(const std::string& input) { return rel2sql::Translate(input); }

// Test function to verify the library is working
bool test_rel2sql() {
  std::string result = rel2sql::Translate("def output {1}");
  return !result.empty();
}

// Emscripten bindings
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(rel2sql_module) {
  emscripten::function("translate", &translate_rel2sql);
  emscripten::function("test", &test_rel2sql);
}
#endif
