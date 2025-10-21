// Browser-compatible loader for Rel2SQL WASM module
// Usage:
//   import { loadRel2Sql } from '@nicovsj/rel2sql-wasm/browser';
//   const mod = await loadRel2Sql();

// Browser-compatible loading - no Node.js imports
export async function loadRel2Sql(options = {}) {
  const { baseUrl, emscripten } = options;

  // Browser-specific loading - no Node.js imports
  const wasmUrl = baseUrl ? `${baseUrl}rel2sql_embindings_browser.wasm` : './dist/rel2sql_embindings_browser.wasm';

  // Create a script element to load the browser build
  return new Promise((resolve, reject) => {
    // Check if we're in a browser environment
    if (typeof window === 'undefined') {
      reject(new Error('Browser loader can only be used in browser environments'));
      return;
    }

    // Create script element to load the browser build
    const script = document.createElement('script');
    script.type = 'text/javascript';
    script.src = './dist/rel2sql_embindings_browser.js';

    script.onload = () => {
      // The browser build creates a global Rel2SqlModule function
      if (typeof window.Rel2SqlModule === 'function') {
        window.Rel2SqlModule({
          locateFile: (path) => {
            if (path.endsWith('.wasm')) return wasmUrl;
            return path;
          },
          ...emscripten,
        }).then(resolve).catch(reject);
      } else {
        reject(new Error('Rel2SqlModule not found after script load'));
      }
    };

    script.onerror = () => {
      reject(new Error('Failed to load rel2sql_embindings_browser.js'));
    };

    document.head.appendChild(script);
  });
}

export default loadRel2Sql;
