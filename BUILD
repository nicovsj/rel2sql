# BUILD
load("//bazel:antlr.bzl", "antlr_cc_library")

package(default_visibility = ["//visibility:public"])

antlr_cc_library(
    name = "rel",
    lexer_src = "src/parser/grammar/CoreRelLexer.g4",
    package = "rel_parser",
    parser_src = "src/parser/grammar/PrunedCoreRelParser.g4",
)

cc_library(
    name = "rel2sql_lib",
    srcs = [
        "src/parser/visitors/arity_visitor.cc",
        "src/parser/visitors/ids_visitor.cc",
        "src/parser/visitors/lit_visitor.cc",
        "src/parser/visitors/sql_visitor.cc",
        "src/parser/visitors/vars_visitor.cc",
        "src/sql.cc",
    ],
    hdrs = [
        "src/parser/extended_ast.h",
        "src/parser/parse.h",
        "src/parser/visitors/arity_visitor.h",
        "src/parser/visitors/base_visitor.h",
        "src/parser/visitors/ids_visitor.h",
        "src/parser/visitors/lit_visitor.h",
        "src/parser/visitors/sql_visitor.h",
        "src/parser/visitors/vars_visitor.h",
        "src/sql.h",
        "src/utils.h",
    ],
    strip_include_prefix = "src",
    deps = [
        "//:rel_cc_parser",
        "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
        "@fmt",
        "@googletest//:gtest",  # Needed for testing private member functions
        "@spdlog",
    ],
)

cc_binary(
    name = "rel2sql",
    srcs = ["main/main.cc"],
    deps = [
        ":rel2sql_lib",
        "@fmt",
        "@spdlog",
    ],
)
