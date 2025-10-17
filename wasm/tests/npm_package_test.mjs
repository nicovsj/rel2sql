import { execSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync, rmSync, cpSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const rootDir = join(__dirname, '..', '..');
const pkgDir = join(rootDir, 'packages', 'rel2sql-wasm');
const distDir = join(pkgDir, 'dist');
const tmpDir = join(process.env.TMPDIR || '/tmp', `rel2sql-consumer-${Date.now()}`);

function need(bin) {
  try {
    execSync(`${bin} --version`, { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
}

// Allow running under Bazel sh_test: if tools are missing, skip instead of failing.
if (!need('node') || !need('npm')) {
  console.error('Required tools (node, npm) not found; skipping package consumption test.');
  process.exit(0);
}

console.log('[1/5] Building WASM with Bazel...');
execSync('bazel build //:rel2sql_wasm --config=emcc', { stdio: 'inherit', cwd: rootDir });

console.log('[2/5] Staging dist files...');
if (existsSync(distDir)) rmSync(distDir, { recursive: true, force: true });
mkdirSync(distDir, { recursive: true });
cpSync(join(rootDir, 'bazel-bin', 'rel2sql_wasm', 'rel2sql_embindings.js'), join(distDir, 'rel2sql_embindings.js'));
cpSync(join(rootDir, 'bazel-bin', 'rel2sql_wasm', 'rel2sql_embindings.wasm'), join(distDir, 'rel2sql_embindings.wasm'));
cpSync(join(pkgDir, 'loader.js'), join(distDir, 'loader.js'));

console.log('[3/5] Packing npm tarball...');
const tarball = execSync('npm pack --silent', { cwd: pkgDir }).toString().trim();
const tarballPath = join(pkgDir, tarball);

console.log('[4/5] Creating temporary ESM consumer and installing tarball...');
if (existsSync(tmpDir)) rmSync(tmpDir, { recursive: true, force: true });
mkdirSync(tmpDir, { recursive: true });
execSync('npm init -y', { cwd: tmpDir, stdio: 'ignore' });
const pkgJsonPath = join(tmpDir, 'package.json');
const pkgJson = JSON.parse(readFileSync(pkgJsonPath, 'utf8'));
pkgJson.type = 'module';
writeFileSync(pkgJsonPath, JSON.stringify(pkgJson, null, 2));
execSync(`npm install "${tarballPath}"`, { cwd: tmpDir, stdio: 'inherit' });

const testFile = join(tmpDir, 'test.mjs');
const testRunnerPath = join(__dirname, 'npm_package_test_runner.mjs');
cpSync(testRunnerPath, testFile);

console.log('[5/5] Running consumption test...');
execSync(`node ${testFile}`, { stdio: 'inherit' });

console.log('All good ✅');
