#!/usr/bin/env node
/**
 * WASM Bindings Test
 *
 * Tests the WASM bindings contract by directly loading artifacts from bazel-bin.
 * This test verifies that all exposed bindings work correctly and the API contract
 * doesn't break.
 */

import { test, describe, before } from "node:test";
import { strict as assert } from "node:assert";
import { loadWasmModule } from "./load_wasm_module.mjs";

// Module will be loaded once and reused across all tests
let Module;

// Setup: Load WASM module before all tests
before(async () => {
  Module = await loadWasmModule();
});

// Test suites organized by functionality
describe("WASM Bindings Contract Tests", () => {
  describe("Translation Functions", () => {
    test("translate function exists", () => {
      assert.strictEqual(typeof Module.translate, "function");
    });

    test("translate returns string for valid input", () => {
      const result = Module.translate("def output {1}");
      assert.strictEqual(typeof result, "string");
      assert(result.length > 0);
    });

    test("translateWithEDB function exists", () => {
      assert.strictEqual(typeof Module.translateWithEDB, "function");
    });

    test("translateWithEDB works with EDBMap", () => {
      const map = new Module.EDBMap();
      const vec = new Module.StringVector();
      vec.push_back("x");
      vec.push_back("y");
      const edbInfo = new Module.EDBInfo(vec);
      map.set("ExternalRelation", edbInfo);

      const result = Module.translateWithEDB("def output { ExternalRelation(x, y) }", map);
      assert.strictEqual(typeof result, "string");
      assert(result.length > 0);
    });
  });

  describe("EDBMap Class", () => {
    test("EDBMap class exists", () => {
      assert.strictEqual(typeof Module.EDBMap, "function");
    });

    test("EDBMap creation and basic methods", () => {
      const map = new Module.EDBMap();
      assert.strictEqual(map.size(), 0);

      const vec = new Module.StringVector();
      vec.push_back("attr1");
      vec.push_back("attr2");
      const edbInfo = new Module.EDBInfo(vec);
      map.set("testRelation", edbInfo);

      assert.strictEqual(map.size(), 1);
      assert(map.has("testRelation"));
      assert(!map.has("nonexistent"));

      const retrieved = map.get("testRelation");
      assert.notStrictEqual(retrieved, undefined);
      assert.strictEqual(retrieved.arity(), 2);
    });
  });

  describe("EDBInfo Class", () => {
    test("EDBInfo class exists", () => {
      assert.strictEqual(typeof Module.EDBInfo, "function");
    });

    test("EDBInfo with named attributes", () => {
      const vec = new Module.StringVector();
      vec.push_back("name");
      vec.push_back("age");
      vec.push_back("city");
      const edbInfo = new Module.EDBInfo(vec);
      assert.strictEqual(edbInfo.arity(), 3);
      assert(edbInfo.hasCustomNamedAttributes());
      assert.strictEqual(edbInfo.getAttributeName(0), "name");
      assert.strictEqual(edbInfo.getAttributeName(1), "age");
      assert.strictEqual(edbInfo.getAttributeName(2), "city");
    });
  });

  describe("Exception Handling", () => {
    test("translate throws parse exception that surfaces to JS", () => {
      const sp = Module.stackSave ? Module.stackSave() : null;
      let threw = false;
      try {
        Module.translate("def F { G(x)"); // Missing closing brace
      } catch (e) {
        threw = true;
        // Use Emscripten helpers per docs to extract type/message
        if (Module.getExceptionMessage) {
          const pair = Module.getExceptionMessage(e);
          const type = pair && pair[0] ? String(pair[0]) : "";
          const msg = pair && pair[1] ? String(pair[1]) : String(e);
          // Expect parse/syntax in either the type or message
          assert(/parse|syntax/i.test(type + " " + msg));
        } else {
          const msg = String(e.message || e);
          assert(/parse|syntax/i.test(msg));
        }
      } finally {
        if (sp !== null && Module.stackRestore) Module.stackRestore(sp);
      }
      assert.strictEqual(threw, true);
    });

    test("translate throws semantic exception (undefined relation)", () => {
      const sp = Module.stackSave ? Module.stackSave() : null;
      let threw = false;
      try {
        Module.translate("def F { G(x) }");
      } catch (e) {
        threw = true;
        if (Module.getExceptionMessage) {
          const pair = Module.getExceptionMessage(e);
          const type = pair && pair[0] ? String(pair[0]) : "";
          const msg = pair && pair[1] ? String(pair[1]) : String(e);
          assert(/undefined|semantic|relation|arity/i.test(type + " " + msg));
        } else {
          const msg = String(e.message || e);
          assert(/undefined|semantic|relation|arity/i.test(msg));
        }
      } finally {
        if (sp !== null && Module.stackRestore) Module.stackRestore(sp);
      }
      assert.strictEqual(threw, true);
    });
  });
});
