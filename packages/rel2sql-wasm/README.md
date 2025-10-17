# @your-scope/rel2sql-wasm

Private package distributing Rel2SQL WebAssembly artifacts and a small loader.

Install (configure GitHub Packages in .npmrc):

```
@your-scope:registry=https://npm.pkg.github.com
//npm.pkg.github.com/:_authToken=${GH_TOKEN}
```

Usage:

```js
import { loadRel2Sql } from '@your-scope/rel2sql-wasm';
const mod = await loadRel2Sql();
```
