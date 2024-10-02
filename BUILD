load("@pybind11_bazel//:build_defs.bzl", "pybind_extension")

# BUILD
load("@rules_java//java:defs.bzl", "java_binary")
load("@rules_python//python:defs.bzl", "py_library", "py_test")
load("//:antlr.bzl", "antlr_cc_library")

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
    name = "rel2sql_lib",
    hdrs = [
        "include/rel2sql.h",
    ],
    include_prefix = "rel2sql",
    includes = ["include"],
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
    deps = [
        "//src/visitors",
    ],
)

cc_binary(
    name = "rel2sql",
    srcs = ["app/main.cc"],
    deps = [
        ":rel2sql_lib",
        "@fmt",
        "@spdlog",
    ],
)

pybind_extension(
    name = "pyrel2sql",
    srcs = ["src/python_bindings.cpp"],
    deps = [
        ":rel2sql_lib"
    ],
)

py_library(
    name = "rel2sql_pylib",
    data = [
        ":pyrel2sql",
    ],
    imports = [
        ".",
    ],
)

py_test(
    name = "test_equality",
    srcs = ["test_equality.py"],
    deps = [
        ":rel2sql_pylib",
    ],
)
