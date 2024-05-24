# BUILD
load("@rules_cc//cc:defs.bzl", "cc_binary")
load("//bazel:antlr.bzl", "antlr_cc_library")

package(default_visibility = ["//visibility:public"])

antlr_cc_library(
    name = "rel",
    src = "src/parser/Rel.g4",
    package = "rel_parser",
)

cc_binary(
    name = "rel-to-sql",
    srcs = ["src/main.cc"],
    deps = [
        "//:rel_cc_parser",
        "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
    ],
)
