# cspell:disable

# BUILD
load("//:antlr.bzl", "antlr_cc_library")
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
    lexer_src = "grammar/RelLexer.g4",
    package = "rel_parser",
    parser_src = "grammar/RelParser.g4",
)

antlr_cc_library(
    name = "sql_parser",
    lexer_src = "grammar/SqlLexer.g4",
    package = "sql_parser",
    parser_src = "grammar/SqlParser.g4",
)

cc_library(
    name = "rel2sql",
    hdrs = ["include/rel2sql/rel2sql.h"],
    srcs = glob([
        "src/api/*.cc",
        "src/api/*.h",
        "src/parser/*.cc",
        "src/parser/*.h",
        "src/preprocessing/*.cc",
        "src/preprocessing/*.h",
        "src/rel_ast/*.cc",
        "src/rel_ast/*.h",
        "src/sql/*.cc",
        "src/sql/*.h",
        "src/sql_ast/*.cc",
        "src/sql_ast/*.h",
        "src/support/*.cc",
        "src/support/*.h",
        "src/optimizer/*.cc",
        "src/optimizer/*.h",
    ]),
    includes = ["include", "src"],
    visibility = ["//visibility:public"],
    deps = [
        "//:rel_parser",
        "//:sql_parser",
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
