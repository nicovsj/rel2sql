# cspell:disable

# BUILD
load("//:antlr.bzl", "antlr_cc_library")
load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")
load("@aspect_rules_js//js:defs.bzl", "js_test")
load("@rules_java//java:java_binary.bzl", "java_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

package(default_visibility = ["//visibility:public"])

java_binary(
    name = "antlr4_tool",
    main_class = "org.antlr.v4.Tool",
    runtime_deps = ["@antlr4_jar//jar"],
)

antlr_cc_library(
    name = "rel_parser",
    lexer_src = "grammar/CoreRelLexer.g4",
    package = "rel_parser",
    parser_src = "grammar/PrunedCoreRelParser.g4",
)

cc_library(
    name = "rel2sql",
    hdrs = ["include/rel2sql/rel2sql.h"],
    srcs = [
        "src/rel2sql.cc",
        "src/sql_visitor.cc",
        "src/translate.h",
        "src/sql_visitor.h",
    ] + glob([
        "src/optimizer/*.cc",
        "src/structs/*.cc",
        "src/preproc/*.cc",
        "src/optimizer/*.h",
        "src/structs/*.h",
        "src/preproc/*.h",
        "src/utils/*.h",
    ]),
    includes = ["include", "src"],
    visibility = ["//visibility:public"],
    deps = [
        "//:rel_parser",
        "@googletest//:gtest_main",
        "@fmt",
    ],
)

cc_shared_library(
    name = "rel2sql_shared",
    deps = [":rel2sql", ":rel_parser"],
)

cc_binary(
    name = "rel2sql_bin",
    srcs = ["apps/cli/main.cc"],
    deps = [":rel2sql"]
)

# Emscripten configuration for WASM build
DEFAULT_EMSCRIPTEN_LINKOPTS = [
    "--bind",
    "-s MODULARIZE=1",
    "-s EXPORT_NAME=Rel2SqlModule",
    "-s EXPORT_ES6=1",
    "-s MALLOC=emmalloc",
    "-s ALLOW_MEMORY_GROWTH=1",
    "-s ASSERTIONS=0",
    "-s USE_PTHREADS=0",
    "-s DISABLE_EXCEPTION_CATCHING=0",
    "-s ENVIRONMENT=node,web",
    "--no-shared-memory",
    "--no-import-memory",
]

WASM_LINKOPTS = [
    "-s WASM=1",
]

# WASM wrapper that directly links against rel2sql library
cc_binary(
    name = "rel2sql_embindings",
    srcs = ["wasm/bindings.cc"],
    deps = [
        ":rel2sql",  # Direct dependency on rel2sql
    ],
    linkopts = DEFAULT_EMSCRIPTEN_LINKOPTS + WASM_LINKOPTS,
    tags = ["manual"],
)

# Produces rel2sql_embindings.{js,wasm,wasm.map}
wasm_cc_binary(
    name = "rel2sql_wasm",
    cc_target = ":rel2sql_embindings",
    tags = ["manual"],
)

# Automated test for WASM module using Node.js
js_test(
    name = "test_wasm_module",
    entry_point = "wasm/test_wasm.js",
    data = [":rel2sql_wasm"],
    tags = ["manual"],  # Manual tag since it requires WASM build
)

# Node.js-based npm package consumption test
js_test(
    name = "npm_package_test",
    entry_point = "wasm/tests/npm_package_test.mjs",
    tags = ["manual"],
)
