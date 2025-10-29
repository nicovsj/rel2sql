# cspell:disable

# WASM BUILD
load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

# Emscripten configuration for WASM builds
# Base linkopts shared by all WASM builds
DEFAULT_EMSCRIPTEN_LINKOPTS = [
    "--bind",
    "-s MODULARIZE=1",
    "-s EXPORT_NAME=Rel2SqlModule",
    "-s MALLOC=emmalloc",
    "-s ALLOW_MEMORY_GROWTH=1",
    "-s ASSERTIONS=0",
    "-s USE_PTHREADS=0",
    "-s DISABLE_EXCEPTION_CATCHING=0",
    "-s EXPORT_EXCEPTION_HANDLING_HELPERS=1",
    "--no-shared-memory",
    "--no-import-memory",
]

# Browser-specific linkopts
BROWSER_EMSCRIPTEN_LINKOPTS = [
    "-s EXPORT_ES6=0",  # No ES6 modules for browser compatibility
    "-s ENVIRONMENT=web",  # Browser only
    "-s SINGLE_FILE=1",  # Bundle WASM into JS for self-contained package
]

# Node.js-specific linkopts
NODE_EMSCRIPTEN_LINKOPTS = [
    "-s EXPORT_ES6=1",  # ES6 modules for Node.js
    "-s ENVIRONMENT=node",  # Node.js only
]

# WASM-specific linkopts
WASM_LINKOPTS = [
    "-s WASM=1",
]

# Combined linkopts for each target
BROWSER_EMSCRIPTEN_LINKOPTS_FULL = DEFAULT_EMSCRIPTEN_LINKOPTS + BROWSER_EMSCRIPTEN_LINKOPTS
NODE_EMSCRIPTEN_LINKOPTS_FULL = DEFAULT_EMSCRIPTEN_LINKOPTS + NODE_EMSCRIPTEN_LINKOPTS

# Browser WASM build
cc_binary(
    name = "rel2sql_embindings_browser",
    srcs = ["bindings.cc"],
    deps = [
        "//:rel2sql",  # Direct dependency on rel2sql
    ],
    linkopts = BROWSER_EMSCRIPTEN_LINKOPTS_FULL + WASM_LINKOPTS,
    tags = ["manual"],
)

# Node.js WASM build
cc_binary(
    name = "rel2sql_embindings_node",
    srcs = ["bindings.cc"],
    deps = [
        "//:rel2sql",  # Direct dependency on rel2sql
    ],
    linkopts = NODE_EMSCRIPTEN_LINKOPTS_FULL + WASM_LINKOPTS,
    tags = ["manual"],
)

# Browser WASM binary
wasm_cc_binary(
    name = "rel2sql_wasm_browser",
    cc_target = ":rel2sql_embindings_browser",
    tags = ["manual"],
)

# Node.js WASM binary
wasm_cc_binary(
    name = "rel2sql_wasm_node",
    cc_target = ":rel2sql_embindings_node",
    tags = ["manual"],
)

# Legacy target for backward compatibility
wasm_cc_binary(
    name = "rel2sql_wasm",
    cc_target = ":rel2sql_embindings_node",
    tags = ["manual"],
)
