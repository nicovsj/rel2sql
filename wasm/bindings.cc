#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#endif
#include <string>

// Simple WASM wrapper without external dependencies
// This demonstrates the two-module approach works

std::string translate_rel2sql(const std::string& input) {
  // Mock implementation for now
  return "WASM: " + input;
}

bool test_rel2sql() { return true; }

#ifdef __EMSCRIPTEN__
// Emscripten bindings
EMSCRIPTEN_BINDINGS(rel2sql_module) {
  // Basic translation functions
  emscripten::function("translate", &translate_rel2sql);

  // Utility functions
  emscripten::function("test", &test_rel2sql);
}
#endif
