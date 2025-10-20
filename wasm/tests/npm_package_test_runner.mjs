import { loadRel2Sql } from '@your-scope/rel2sql-wasm';

const mod = await loadRel2Sql();
const out = mod.translate('def output {1}');
if (!out || typeof out !== 'string') throw new Error('Unexpected translate output');
console.log('OK:', out.substring(0, 60));
