# cspell:disable

# BUILD
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
    name = "rel2sql",
    hdrs = [
        "src/rel2sql.h",
        "src/translate.h",
        "src/sql_visitor.h",
    ] + glob([
        "src/optimizer/*.h",
        "src/structs/*.h",
        "src/preproc/*.h",
        "src/utils/*.h",
    ]),
    srcs = [
        "src/sql_visitor.cc",
    ] + glob([
        "src/optimizer/*.cc",
        "src/structs/*.cc",
        "src/preproc/*.cc",
    ]),
    include_prefix = "rel2sql",
    includes = ["src"],
    strip_include_prefix = "src",
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
    srcs = ["app/main.cc"],
    deps = [":rel2sql"],
    dynamic_deps = [":rel2sql_shared"],
)
