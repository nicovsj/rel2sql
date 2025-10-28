#!/bin/bash
set -e

PKG_DIR=packages/rel2sql-wasm
DIST=$PKG_DIR/dist
mkdir -p $DIST

# Copy Node.js build (main build)
cp -v bazel-bin/wasm/rel2sql_wasm_node/rel2sql_embindings_node.js $DIST/rel2sql_embindings.js
cp -v bazel-bin/wasm/rel2sql_wasm_node/rel2sql_embindings_node.wasm $DIST/rel2sql_embindings_node.wasm

# Copy browser build
cp -v bazel-bin/wasm/rel2sql_wasm_browser/rel2sql_embindings_browser.js $DIST/rel2sql_embindings_browser.js
cp -v bazel-bin/wasm/rel2sql_wasm_browser/rel2sql_embindings_browser.wasm $DIST/rel2sql_embindings_browser.wasm

# Copy loaders
cp -v $PKG_DIR/loader-node.js $DIST/loader-node.js
cp -v $PKG_DIR/loader-browser.js $DIST/loader-browser.js
