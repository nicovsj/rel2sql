#!/usr/bin/env node
/**
 * NPM Package Smoke Test (Browser Loader)
 *
 * Basic smoke test to verify the browser loader can be imported.
 * Simulates a browser environment to test loader interface.
 */

// Simulate browser environment for testing
global.window = {};
global.document = { head: { appendChild: () => {} } };

import { loadRel2Sql } from "@nicovsj/rel2sql-wasm/browser";

// Test that the browser loader can be imported and has the right interface
if (typeof loadRel2Sql !== "function") {
  throw new Error("loadRel2Sql should be a function");
}

console.log("Browser loader interface OK - function exported correctly");
console.log("Note: Full browser test requires actual browser environment");
