# Rel2SQL WebAssembly (WASM) Module

This directory contains the WebAssembly bindings and testing infrastructure for the Rel2SQL project, enabling Rel2SQL to run in web browsers.

## 📁 Files

- **`bindings.cc`**: C++ bindings that expose Rel2SQL functionality to JavaScript via Emscripten
- **`test_wasm.html`**: Interactive test page for the WASM module
- **`README.md`**: This documentation file

## 🏗️ Build System

### Bazel Targets

The WASM functionality is integrated into the main Bazel build system:

- **`//:rel2sql_embindings`**: C++ binary that links Rel2SQL with Emscripten bindings
- **`//:rel2sql_wasm`**: Final WASM module that produces `.js` and `.wasm` files
- **`//:test_wasm_module`**: Automated Node.js test that verifies WASM functionality

### Generated Artifacts

When you build the WASM target, the following files are generated in `bazel-bin/rel2sql_wasm/`:

- **`rel2sql_embindings.js`**: JavaScript wrapper that loads and initializes the WASM module
- **`rel2sql_embindings.wasm`**: The actual WebAssembly binary
- **`rel2sql_embindings.wasm.map`**: Source map for debugging
- **`rel2sql_embindings.js.mem`**: Memory management files
- **`rel2sql_embindings.html`**: Auto-generated HTML template (not used)

## 🚀 Building and Testing

### Quick Start

```bash
# Build the WASM module and start test server
task test-wasm
```

This command will:
1. Build the WASM module using `--config=emcc`
2. Kill any existing process on port 8000
3. Start a local HTTP server
4. Open `http://localhost:8000/wasm/test_wasm.html` in your browser

### Manual Build

```bash
# Build only the WASM module
bazel build //:rel2sql_wasm --config=emcc

# Or use the task
task build-wasm
```

### Automated Testing (CI/CI)

For automated testing in CI workflows:

```bash
# Run automated Bazel test
task test-wasm-bazel

# Or directly with Bazel
bazel build //:rel2sql_wasm --config=emcc
bazel test //:test_wasm_module --config=emcc
```

This automated test uses [aspect-build/rules_js](https://github.com/aspect-build/rules_js) for proper Node.js integration:
- Verifies WASM files are generated correctly
- Loads the WASM module in Node.js using proper module resolution
- Runs system tests to ensure basic functionality
- Tests translation of sample Rel expressions
- Reports pass/fail status for CI integration
- Uses clean Node.js test infrastructure (no shell script hacks)

### Manual Testing

```bash
# Start HTTP server manually
python3 -m http.server 8000

# Then open http://localhost:8000/wasm/test_wasm.html
```

## 🧪 Testing Interface

The `test_wasm.html` page provides:

### Features
- **Interactive Rel-to-SQL Translation**: Enter Rel expressions and see SQL output
- **System Test**: Verify the WASM module is working correctly
- **Real-time Status**: Shows loading status and error messages
- **Modern UI**: Clean, responsive interface with good UX

### Usage
1. Open the test page in your browser
2. Wait for the "✅ WASM module loaded successfully!" message
3. Try translating Rel expressions like `def output {1}`
4. Run the system test to verify functionality

## 🔧 Technical Details

### Emscripten Configuration

The WASM build uses the following Emscripten flags:

```bazel
DEFAULT_EMSCRIPTEN_LINKOPTS = [
    "--bind",                    # Enable Embind for C++/JS interop
    "-s MODULARIZE=1",          # Export as a factory function
    "-s EXPORT_NAME=Rel2SqlModule",  # Name of the exported factory
    "-s MALLOC=emmalloc",       # Use emmalloc for memory management
    "-s ALLOW_MEMORY_GROWTH=1", # Allow dynamic memory growth
    "-s ASSERTIONS=0",          # Disable runtime assertions for performance
    "-s USE_PTHREADS=0",        # Disable threading support
    "-s DISABLE_EXCEPTION_CATCHING=0",  # Enable exception handling
    "--no-shared-memory",       # Disable shared memory
    "--no-import-memory",       # Don't import memory from host
]
```

### Module Loading

The WASM module is loaded using a script tag approach:

```html
<script src="./bazel-bin/rel2sql_wasm/rel2sql_embindings.js"></script>
<script>
    // Rel2SqlModule is available globally
    const module = await Rel2SqlModule();
    // Use module.translate() and module.test()
</script>
```

### Exposed Functions

The bindings expose these JavaScript functions:

- **`translate(relExpression: string): string`**: Translates a Rel expression to SQL
- **`test(): boolean`**: Tests if the module is working correctly

## 🐛 Troubleshooting

### Common Issues

1. **Port 8000 already in use**:
   ```bash
   kill $(lsof -ti:8000)
   ```

2. **WASM module not loading**:
   - Check browser console for errors
   - Ensure the server is running and serving files correctly
   - Verify the WASM build completed successfully

3. **Functions not available**:
   - Make sure the module finished loading (check status indicator)
   - Try refreshing the page
   - Check browser console for initialization errors

### Debug Mode

To debug WASM issues:

1. Open browser Developer Tools (F12)
2. Check the Console tab for error messages
3. Use the Network tab to verify WASM files are loading
4. Check the status indicator on the test page

## 📚 Dependencies

The WASM build requires:

- **Emscripten SDK**: Version 4.0.13 (managed by Bazel)
- **Emscripten Bazel Integration**: `@emsdk` module
- **Compatible Dependencies**:
  - `fmt` 9.1.0 (for Emscripten compatibility)
  - `spdlog` 1.11.0 (compatible with fmt 9.1.0)

## 🔄 Integration with Main Project

The WASM functionality is fully integrated with the main Rel2SQL project:

- Uses the same source code and dependencies
- Builds alongside native targets
- Shares the same Bazel configuration
- Maintains consistency with the main API

## 📖 Further Reading

- [Emscripten Documentation](https://emscripten.org/)
- [Emscripten Embind](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/embind.html)
- [WebAssembly MDN Documentation](https://developer.mozilla.org/en-US/docs/WebAssembly)
- [Bazel Emscripten Integration](https://github.com/emscripten-core/emsdk)
