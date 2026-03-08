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

cc_test(
    name = "test_translation",
    srcs = [
        "test_translation.cc",
    ],
    deps = [
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
    name = "test_optimization",
    srcs = [
        "test_optimization.cc",
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
