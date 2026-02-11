load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

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

cc_test(
    name = "test_translation",
    srcs = [
        "test_translation.cc",
    ],
    deps = [
        "//:rel2sql",
        ":test_common",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_sql_ast",
    srcs = [
        "test_sql_ast.cc",
    ],
    deps = [
        "//:rel2sql",
        ":test_common",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_optimization",
    srcs = [
        "test_optimization.cc",
    ],
    deps = [
        "//:rel2sql",
        ":test_common",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_bound_set",
    srcs = [
        "test_bound_set.cc",
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
        "//:rel2sql",
        ":test_common",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_sql_parser",
    srcs = [
        "test_sql_parser.cc",
    ],
    deps = [
        "//:rel2sql",
        ":test_common",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_rel_ast",
    srcs = [
        "test_rel_ast.cc",
    ],
    deps = [
        "//:rel2sql",
        ":test_common",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_sql_parser_roundtrip",
    srcs = [
        "test_sql_parser_roundtrip.cc",
    ],
    deps = [
        "//:rel2sql",
        ":test_common",
        "@googletest//:gtest_main",
    ],
)
