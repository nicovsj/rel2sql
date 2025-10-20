#!/usr/bin/env node

/**
 * Automated test for Rel2SQL WASM module
 * This script tests the WASM module functionality without requiring a browser
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// In Bazel test environment, the WASM files are available in the runfiles
// The data dependency makes them available in the current directory structure
const WASM_JS_PATH = 'rel2sql_wasm/rel2sql_embindings.js';
const WASM_WASM_PATH = 'rel2sql_wasm/rel2sql_embindings.wasm';

// Mock browser environment for Node.js
global.window = {
  location: { href: 'file://' + __dirname }
};
global.document = {
  currentScript: { src: 'file://' + __dirname }
};

// Test cases
const TEST_CASES = [
  {
    name: 'Simple constant expression',
    input: 'def output {1}',
  },
  {
    name: 'Simple tuple',
    input: 'def output {1, 2}',
  }
];

async function testWasmModule() {
  console.log('🧪 Testing Rel2SQL WASM module...\n');

  // Check if WASM files exist
  if (!fs.existsSync(WASM_JS_PATH)) {
    throw new Error(`WASM JS file not found: ${WASM_JS_PATH}`);
  }

  if (!fs.existsSync(WASM_WASM_PATH)) {
    throw new Error(`WASM binary file not found: ${WASM_WASM_PATH}`);
  }

  console.log('✅ WASM files found');

  // Load the WASM module
  console.log('📦 Loading WASM module...');

  try {
    // Import the WASM module using ES6 import
    const wasmPath = path.resolve(WASM_JS_PATH);
    const wasmUrl = `file://${wasmPath}`;
    const Rel2SqlModule = await import(wasmUrl);

    // Initialize the module
    const module = await Rel2SqlModule.default();
    console.log('✅ WASM module loaded successfully');

    // Test the system test function
    console.log('🔍 Running system test...');
    const systemTestResult = module.test();

    if (!systemTestResult) {
      throw new Error('System test failed');
    }
    console.log('✅ System test passed');

    // Test translation functionality
    console.log('🔄 Testing translation functionality...');
    let passedTests = 0;
    let totalTests = TEST_CASES.length;

    for (const testCase of TEST_CASES) {
      try {
        console.log(`  Testing: ${testCase.name}`);
        const result = module.translate(testCase.input);

        if (result && result.trim().length > 0) {
          console.log(`    ✅ Translation successful: "${result}"`);
          passedTests++;
        } else {
          console.log(`    ❌ Translation failed: empty result`);
        }
      } catch (error) {
        console.log(`    ❌ Translation failed: ${error.message}`);
      }
    }

    console.log(`\n📊 Test Results: ${passedTests}/${totalTests} tests passed`);

    if (passedTests === totalTests) {
      console.log('🎉 All tests passed! WASM module is working correctly.');
      return true;
    } else {
      console.log('⚠️  Some tests failed. WASM module may have issues.');
      return false;
    }

  } catch (error) {
    console.error('❌ Failed to load or test WASM module:', error.message);
    throw error;
  }
}

// Run the test
async function main() {
  try {
    const success = await testWasmModule();
    process.exit(success ? 0 : 1);
  } catch (error) {
    console.error('💥 Test suite failed:', error.message);
    process.exit(1);
  }
}

// Run if this script is executed directly
if (import.meta.url === `file://${process.argv[1]}`) {
  main();
}

export { testWasmModule, TEST_CASES };
