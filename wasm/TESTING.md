# Rel2SQL WASM Testing

## Test structure

### Main automated checks

1. **`npm_package_test.mjs`** — CI-style flow: builds Node + browser WASM with Bazel, stages `packages/rel2sql-wasm/dist`, packs the npm package, and smoke-tests consumption.
2. **`bindings_test.mjs`** — Loads artifacts from `bazel-bin` and asserts the Embind API contract (also run in CI after the npm script builds WASM).
3. **`test_browser.html`** — Manual browser checks (performance, errors, loader paths). Served via `task test:wasm:browser` or a local `python3 -m http.server`.

## How to test

### From the repo root (Task)

```bash
# All WASM-related Node steps (bindings + npm + browser server)
task test:wasm

# Individual steps
task test:wasm:bindings   # requires prior: bazel build //wasm:rel2sql_wasm_node --config=emcc
task test:wasm:npm
task test:wasm:browser    # then open http://localhost:8080/wasm/tests/browser/test_browser.html

# Native C++ tests + WASM tasks above
task test:all
```

### Direct Node (same as CI package workflow)

```bash
node wasm/tests/npm_package_test.mjs
node wasm/tests/bindings_test.mjs   # run after WASM is built (npm script builds it)
```

### Manual browser testing

```bash
task test:wasm:browser
# Or:
node wasm/setup_browser_test.mjs
python3 -m http.server 8080
# Open: http://localhost:8080/wasm/tests/browser/test_browser.html
```

## Layout

```
wasm/
├── setup_browser_test.mjs          # Browser test setup
├── tests/
│   ├── bindings_test.mjs           # Embind contract (CI + task test:wasm:bindings)
│   ├── npm_package_test.mjs        # Package build/consumption (CI)
│   ├── load_wasm_module.mjs        # Helper for bindings_test
│   └── browser/
│       ├── test_browser.html       # Manual browser test
│       └── dist/                   # Generated (gitignored)
│           ├── loader-browser.js
│           ├── rel2sql_embindings_browser.js
│           └── rel2sql_embindings_browser.wasm
└── TESTING.md                      # This file
```

## Coverage

- Node.js WASM build — `npm_package_test.mjs`, `bindings_test.mjs`
- Browser WASM build — `npm_package_test.mjs`, manual HTML
- NPM package layout and consumption — `npm_package_test.mjs`

## Historical cleanup

Removed earlier helpers (`test_browser_setup.mjs`, `test_npm_package_local.mjs`, `run_tests.mjs`, etc.) in favor of the flows above.
