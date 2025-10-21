// Browser-compatible loader for Rel2SQL WASM module
// Usage:
//   import { loadRel2Sql } from '@nicovsj/rel2sql-wasm/browser';
//   const mod = await loadRel2Sql();

// Browser-compatible loading using dynamic imports
export async function loadRel2Sql(options = {}) {
  const { emscripten } = options;

  // Check if we're in a browser environment
  if (typeof window === 'undefined') {
    throw new Error('Browser loader can only be used in browser environments');
  }

  try {
    // Dynamic import of the browser build JS file
    const wasmModule = await import('./rel2sql_embindings_browser.js');
    const Rel2SqlModule = wasmModule.default;

    if (typeof Rel2SqlModule !== 'function') {
      throw new Error('Rel2SqlModule factory not found in browser build');
    }

    // Load WASM binary as a module (bundled with SINGLE_FILE=1)
    const wasmBinary = await import('./rel2sql_embindings_browser.wasm');

    // Instantiate the WASM module
    const instance = await Rel2SqlModule({
      wasmBinary: wasmBinary.default,
      ...emscripten,
    });

    return instance;
  } catch (error) {
    throw new Error(`Failed to load browser WASM module: ${error.message}`);
  }
}

export default loadRel2Sql;
