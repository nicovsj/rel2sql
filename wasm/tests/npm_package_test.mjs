import { execSync } from "node:child_process";
import {
  existsSync,
  mkdirSync,
  readFileSync,
  writeFileSync,
  rmSync,
  cpSync,
} from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const rootDir = join(__dirname, "..", "..");
const pkgDir = join(rootDir, "packages", "rel2sql-wasm");
const distDir = join(pkgDir, "dist");
const tmpDir = join(
  process.env.TMPDIR || "/tmp",
  `rel2sql-consumer-${Date.now()}`
);

function need(bin) {
  try {
    execSync(`${bin} --version`, { stdio: "ignore" });
    return true;
  } catch {
    return false;
  }
}

// Allow running under Bazel sh_test: if tools are missing, skip instead of failing.
if (!need("node") || !need("npm")) {
  console.error(
    "Required tools (node, npm) not found; skipping package consumption test."
  );
  process.exit(0);
}

console.log("[1/6] Building WASM with Bazel (Node.js and Browser)...");
execSync(
  "bazel build //wasm:rel2sql_wasm_node //wasm:rel2sql_wasm_browser --config=emcc",
  { stdio: "inherit", cwd: rootDir }
);

console.log("[2/6] Staging dist files...");
if (existsSync(distDir)) rmSync(distDir, { recursive: true, force: true });
mkdirSync(distDir, { recursive: true });

// Copy Node.js build (main build) - use original names as generated
cpSync(
  join(
    rootDir,
    "bazel-bin",
    "wasm",
    "rel2sql_wasm_node",
    "rel2sql_embindings_node.js"
  ),
  join(distDir, "rel2sql_embindings.js")
);
cpSync(
  join(
    rootDir,
    "bazel-bin",
    "wasm",
    "rel2sql_wasm_node",
    "rel2sql_embindings_node.wasm"
  ),
  join(distDir, "rel2sql_embindings_node.wasm")
);

// Copy browser build - use original names as generated
cpSync(
  join(
    rootDir,
    "bazel-bin",
    "wasm",
    "rel2sql_wasm_browser",
    "rel2sql_embindings_browser.js"
  ),
  join(distDir, "rel2sql_embindings_browser.js")
);
cpSync(
  join(
    rootDir,
    "bazel-bin",
    "wasm",
    "rel2sql_wasm_browser",
    "rel2sql_embindings_browser.wasm"
  ),
  join(distDir, "rel2sql_embindings_browser.wasm")
);

// Copy loaders
cpSync(join(pkgDir, "loader-node.js"), join(distDir, "loader-node.js"));
cpSync(join(pkgDir, "loader-browser.js"), join(distDir, "loader-browser.js"));

console.log("[3/6] Packing npm tarball...");
const tarball = execSync("npm pack --silent", { cwd: pkgDir })
  .toString()
  .trim();
const tarballPath = join(pkgDir, tarball);

console.log("[4/6] Creating temporary ESM consumer and installing tarball...");
if (existsSync(tmpDir)) rmSync(tmpDir, { recursive: true, force: true });
mkdirSync(tmpDir, { recursive: true });
execSync("npm init -y", { cwd: tmpDir, stdio: "ignore" });
const pkgJsonPath = join(tmpDir, "package.json");
const pkgJson = JSON.parse(readFileSync(pkgJsonPath, "utf8"));
pkgJson.type = "module";
writeFileSync(pkgJsonPath, JSON.stringify(pkgJson, null, 2));
execSync(`npm install "${tarballPath}"`, { cwd: tmpDir, stdio: "inherit" });

console.log("[5/6] Running Node.js consumption test...");
const testContent = `
import { loadRel2Sql } from "@nicovsj/rel2sql-wasm";

const mod = await loadRel2Sql();
const out = mod.translate("def output {1}");
if (!out || typeof out !== "string")
  throw new Error("Unexpected translate output");
console.log("OK:", out.substring(0, 60));
`;
writeFileSync(join(tmpDir, "test.mjs"), testContent);
execSync(`node ${join(tmpDir, "test.mjs")}`, { stdio: "inherit" });

console.log("[6/6] Testing browser loader (simulated browser environment)...");
const browserTestContent = `
// Simulate browser environment for testing
global.window = {};
global.document = { head: { appendChild: () => {} } };

import { loadRel2Sql } from "@nicovsj/rel2sql-wasm/browser";

// Test that the browser loader can be imported and has the right interface
if (typeof loadRel2Sql !== 'function') {
  throw new Error('loadRel2Sql should be a function');
}

console.log("Browser loader interface OK - function exported correctly");
console.log("Note: Full browser test requires actual browser environment");
`;
writeFileSync(join(tmpDir, "browser-test.mjs"), browserTestContent);
execSync(`node ${join(tmpDir, "browser-test.mjs")}`, { stdio: "inherit" });

console.log("All good ✅");
