#!/usr/bin/env node
/**
 * NPM Package Smoke Test (Node.js)
 *
 * Basic smoke test to verify the npm package can be consumed in Node.js.
 * Tests focus on packaging correctness, not comprehensive binding functionality.
 */

import { loadRel2Sql } from "@nicovsj/rel2sql-wasm";

async function runTest() {
  // Test that the loader function exists and can be called
  if (typeof loadRel2Sql !== "function") {
    throw new Error("loadRel2Sql should be a function");
  }

  // Test that the module can be loaded
  const mod = await loadRel2Sql();

  if (!mod) {
    throw new Error("loadRel2Sql should return a module");
  }

  // Test basic translation works
  const out = mod.translate("def output {1}");

  if (!out || typeof out !== "string") {
    throw new Error("Unexpected translate output - expected non-empty string");
  }

  console.log("OK: Package loaded and basic translation works");
  console.log(`Result preview: ${out.substring(0, 60)}...`);
}

runTest().catch((error) => {
  console.error("Smoke test failed:", error.message);
  process.exit(1);
});
