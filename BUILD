# BUILD
load("@rules_cc//cc:defs.bzl", "cc_binary")
load("//bazel:antlr.bzl", "antlr_cc_library")

package(default_visibility = ["//visibility:public"])

antlr_cc_library(
    name = "rel",
    lexer_src = "src/parser/CoreRelLexer.g4",
    package = "rel_parser",
    parser_src = "src/parser/CoreRelParser.g4",
)

cc_binary(
    name = "rel-to-sql",
    srcs = ["src/main.cc"],
    deps = [
        "//:rel_cc_parser",
        "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
    ],
)

cc_test(
    name = "rel_test",
    srcs = ["tests/my_test.cc"],
    deps = [
        "@googletest//:gtest",
    ],
)
