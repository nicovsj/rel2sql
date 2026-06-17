load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "test_common",
    srcs = [
        "test_common.h",
    ],
    deps = [
        "//:rel2sql",
    ],
)

cc_library(
    name = "duckdb_exec",
    testonly = True,
    srcs = ["duckdb_exec.cc"],
    hdrs = ["duckdb_exec.h"],
    includes = ["."],
    deps = [
        ":duckdb_session",
        "//:rel2sql",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "duckdb_session",
    testonly = True,
    srcs = ["generator/duckdb_session.cc"],
    hdrs = ["generator/duckdb_session.h"],
    includes = ["."],
    deps = [
        "//:rel2sql",
        "//third_party/duckdb:duckdb",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "rel_program_generator",
    testonly = True,
    srcs = [
        "generator/data_fixture.cc",
        "generator/rel_engine.cc",
        "generator/rel_program_generator.cc",
        "generator/result_set.cc",
        "generator/sql_executor.cc",
    ],
    hdrs = [
        "generator/data_fixture.h",
        "generator/rel_engine.h",
        "generator/rel_program_generator.h",
        "generator/result_set.h",
        "generator/rng.h",
        "generator/sql_executor.h",
    ],
    includes = ["."],
    deps = [
        ":duckdb_session",
        ":test_common",
        "//:rel2sql",
        "//third_party/duckdb:duckdb",
    ],
)

cc_test(
    name = "test_generated_correctness",
    srcs = ["test_generated_correctness.cc"],
    deps = [
        ":rel_program_generator",
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_translation",
    srcs = [
        "test_translation.cc",
    ],
    deps = [
        ":duckdb_exec",
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_sql_ast",
    srcs = [
        "test_sql_ast.cc",
    ],
    deps = [
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_safety",
    srcs = [
        "test_safety.cc",
    ],
    deps = [
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_exceptions",
    srcs = [
        "test_exceptions.cc",
    ],
    deps = [
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_sql_parser",
    srcs = [
        "test_sql_parser.cc",
    ],
    deps = [
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_rel_ast",
    srcs = [
        "test_rel_ast.cc",
    ],
    deps = [
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_sql_parser_roundtrip",
    srcs = [
        "test_sql_parser_roundtrip.cc",
    ],
    deps = [
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_rewriter",
    srcs = [
        "test_rewriter.cc",
    ],
    deps = [
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)
