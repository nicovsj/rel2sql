# 🧪 Rel2SQL WASM Testing

## **Simple Test Structure**

### **Essential Tests (2 total):**

1. **`npm_package_test.mjs`** - Main test for CI/CD
   - Builds both Node.js and Browser WASM
   - Creates npm package
   - Tests Node.js consumption
   - Verifies browser build files

2. **`test_browser.html`** - Comprehensive browser test
   - Direct WASM loading
   - Loader-based loading
   - Performance testing
   - Error handling
   - Multiple test scenarios

## **How to Test**

### **For Development:**
```bash
# Main test (recommended)
task test-npm-package

# Browser testing
task test-browser
# Then open: http://localhost:8080/wasm/tests/browser/test_browser_direct.html
```

### **For CI/CD:**
```bash
# Official test
node wasm/tests/npm_package_test.mjs
```

### **Manual Browser Testing:**
```bash
# Setup and start server
task test-browser

# Or manually:
node wasm/setup_browser_test.mjs
python3 -m http.server 8080

# Open browser:
# http://localhost:8080/wasm/tests/browser/test_browser.html
```

## **Test Structure**

```
wasm/
├── setup_browser_test.mjs          # Browser test setup
├── tests/
│   ├── npm_package_test.mjs        # Main test (CI/CD)
│   └── browser/
│       ├── test_browser.html          # Comprehensive browser test
│       └── dist/                        # Generated files (gitignored)
│           ├── loader-browser.js
│           ├── rel2sql_embindings_browser.js
│           └── rel2sql_embindings_browser.wasm
└── TESTING.md                      # This file
```

## **Test Coverage**

✅ **Node.js Build** - Full coverage
✅ **Browser Build** - Full coverage
✅ **NPM Package** - Full coverage
✅ **File Structure** - Full coverage
✅ **Browser Loading** - Full coverage
✅ **Translation** - Full coverage
✅ **Performance** - Full coverage

## **What Was Removed**

❌ `test_browser_setup.mjs` - Redundant temp file creation
❌ `test_npm_package_local.mjs` - Duplicated npm test
❌ `run_tests.mjs` - Overcomplicated runner
❌ Generated temp directories - Cleaned up

**Result: Clean structure with single browser test, dist/ directory for generated files, full coverage, no redundancy!** 🎉
