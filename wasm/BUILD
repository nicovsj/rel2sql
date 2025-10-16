# WASM-specific BUILD file
# This module only contains WASM wrapper code, not the core library

load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")

# Emscripten configuration for WASM build
DEFAULT_EMSCRIPTEN_LINKOPTS = [
    "--bind",
    "-s MODULARIZE=1",
    "-s EXPORT_NAME=Rel2SqlModule",
    "-s MALLOC=emmalloc",
    "-s ALLOW_MEMORY_GROWTH=1",
    "-s ASSERTIONS=0",
    "-s USE_PTHREADS=0",
    "-s DISABLE_EXCEPTION_CATCHING=0",
    "--no-shared-memory",
    "--no-import-memory",
]

WASM_LINKOPTS = [
    "-s WASM=1",
]

# WASM wrapper that uses a pre-built native library
# This avoids recompiling fmt/ANTLR4 with Emscripten
cc_binary(
    name = "rel2sql_wasm_wrapper",
    srcs = ["bindings.cc"],
    linkopts = DEFAULT_EMSCRIPTEN_LINKOPTS + WASM_LINKOPTS,
    # Don't depend on the source - we'll provide a pre-built library
    # deps = [
    #     "@rel2sql_core//:rel2sql",  # This causes fmt to be recompiled with Emscripten
    # ],
    tags = ["manual"],
)

# Produces rel2sql_wasm.{js,wasm,wasm.map}
wasm_cc_binary(
    name = "rel2sql_wasm",
    cc_target = ":rel2sql_wasm_wrapper",
    tags = ["manual"],
)
