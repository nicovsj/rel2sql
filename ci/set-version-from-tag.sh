#!/bin/bash
set -e

TAG=${GITHUB_REF#refs/tags/}
echo "Publishing version $TAG"
jq --arg v "$TAG" '.version=$v' packages/rel2sql-wasm/package.json > packages/rel2sql-wasm/package.json.tmp
mv packages/rel2sql-wasm/package.json.tmp packages/rel2sql-wasm/package.json
