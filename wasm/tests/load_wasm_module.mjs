#!/usr/bin/env node
/**
 * WASM Module Loader Utility
 *
 * Handles loading the Emscripten-generated WASM module from bazel-bin.
 * Run with: node wasm/tests/bindings_test.mjs
 *
 * Make sure to build first: bazel build //wasm:rel2sql_wasm_node --config=emcc
 */

import { existsSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Determine the path to WASM artifacts in bazel-bin
function getWasmDir() {
  const rootDir = join(__dirname, "..", "..");
  return join(rootDir, "bazel-bin", "wasm", "rel2sql_wasm_node");
}

/**
 * Load the WASM module
 * @returns {Promise} Promise that resolves to the initialized WASM module
 */
export async function loadWasmModule() {
  const wasmDir = getWasmDir();
  const jsPath = join(wasmDir, "rel2sql_embindings_node.js");
  const wasmPath = join(wasmDir, "rel2sql_embindings_node.wasm");

  // Verify artifacts exist
  if (!existsSync(jsPath)) {
    throw new Error(
      `WASM artifacts not found.\n` +
      `Checked path: ${jsPath}\n` +
      `Please build first: bazel build //wasm:rel2sql_wasm_node --config=emcc`
    );
  }

  if (!existsSync(wasmPath)) {
    throw new Error(`WASM file not found at: ${wasmPath}`);
  }

  console.log(`Loading WASM module from bazel-bin...`);
  console.log(`WASM directory: ${wasmDir}`);

  // Import the Emscripten-generated ES module
  const moduleUrl = pathToFileURL(jsPath).href;
  const ModuleFactory = await import(moduleUrl);

  // Initialize the module with locateFile callback
  // locateFile should return file system paths (not URLs)
  const Module = await ModuleFactory.default({
    locateFile: (path) => {
      if (path === "rel2sql_embindings_node.wasm") {
        return wasmPath;
      }
      return join(wasmDir, path);
    },
  });

  console.log("Module loaded successfully\n");
  return Module;
}
