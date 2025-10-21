#!/usr/bin/env node

import { execSync } from "node:child_process";
import { existsSync, mkdirSync, cpSync, rmSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const rootDir = join(__dirname, "..");
const testDir = join(__dirname, "tests", "browser");

console.log("🔧 Setting up browser test files...");

// Step 1: Build WASM modules
console.log("[1/3] Building WASM modules...");
try {
  execSync("bazel build //wasm:rel2sql_wasm_browser --config=emcc", {
    stdio: "inherit",
    cwd: rootDir,
  });
  console.log("✅ WASM modules built successfully");
} catch (error) {
  console.error("❌ Failed to build WASM modules:", error.message);
  process.exit(1);
}

// Step 2: Copy browser build files to test dist directory
console.log("[2/3] Copying browser build files...");
const distDir = join(testDir, "dist");
if (existsSync(distDir)) rmSync(distDir, { recursive: true, force: true });
mkdirSync(distDir, { recursive: true });

const browserJsPath = join(
  rootDir,
  "bazel-bin",
  "wasm",
  "rel2sql_wasm_browser",
  "rel2sql_embindings_browser.js"
);
const browserWasmPath = join(
  rootDir,
  "bazel-bin",
  "wasm",
  "rel2sql_wasm_browser",
  "rel2sql_embindings_browser.wasm"
);
const loaderPath = join(
  rootDir,
  "packages",
  "rel2sql-wasm",
  "loader-browser.js"
);

if (
  existsSync(browserJsPath) &&
  existsSync(browserWasmPath) &&
  existsSync(loaderPath)
) {
  cpSync(browserJsPath, join(distDir, "rel2sql_embindings_browser.js"));
  cpSync(browserWasmPath, join(distDir, "rel2sql_embindings_browser.wasm"));
  cpSync(loaderPath, join(distDir, "loader-browser.js"));
  console.log("✅ Browser test files copied to dist/");
} else {
  console.error("❌ Required files not found");
  process.exit(1);
}

console.log("[3/3] Setup complete!");
console.log("");
console.log("🌐 To test browser functionality:");
console.log("1. Start HTTP server: python3 -m http.server 8080");
console.log(
  "2. Open browser: http://localhost:8080/wasm/tests/browser/test_browser.html"
);
