load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_test(
    name = "test_extended_ast",
    srcs = [
        "test_extended_ast.cc",
    ],
    deps = [
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
        "@googletest//:gtest_main",
    ],
)
