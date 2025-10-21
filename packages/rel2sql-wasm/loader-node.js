// Node.js-compatible loader for Rel2SQL WASM module published via GitHub Packages
// Usage:
//   import { loadRel2Sql } from '@nicovsj/rel2sql-wasm';
//   const mod = await loadRel2Sql();

import Rel2SqlModule from './rel2sql_embindings.js';

export async function loadRel2Sql(options = {}) {
  const { baseUrl, emscripten } = options;
  const resolvedBaseUrl = baseUrl ?? new URL('./', import.meta.url);

  const Module = await Rel2SqlModule({
    locateFile: (path) => {
      // Handle the specific case where the generated JS looks for rel2sql_embindings_node.wasm
      if (path === 'rel2sql_embindings_node.wasm') {
        return new URL('rel2sql_embindings_node.wasm', resolvedBaseUrl).toString();
      }
      return new URL(path, resolvedBaseUrl).toString();
    },
    ...emscripten,
  });

  return Module;
}

export default loadRel2Sql;
