# BUILD
load("@rules_cc//cc:defs.bzl", "cc_binary")
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
        "src/parser/fv_visitor.cc",
        "src/parser/sql_visitor.cc",
        "src/sql.cc",
    ],
    hdrs = [
        "src/parser/fv_visitor.h",
        "src/parser/sql_visitor.h",
        "src/parser/parse.h",
        "src/sql.h",
        "src/utils.h",
    ],
    strip_include_prefix = "src",
    deps = [
        "//:rel_cc_parser",
        "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
        "@fmt",
        "@spdlog",
    ],
)

cc_test(
    name = "rel_test",
    srcs = [
        "tests/test_free_vars.cc",
        "tests/test_sql_ast.cc",
        "tests/test_translation.cc"
    ],
    deps = [
        ":rel2sql_lib",
        "@googletest//:gtest_main",
        "@spdlog",
    ],
)
