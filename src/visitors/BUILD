cc_library(
    name = "visitors",
    srcs = glob(["*.cc"]),
    hdrs = glob(["*.h"]),
    include_prefix = "visitors",
    visibility = ["//visibility:public"],
    deps = [
        "//src/parser:parser",
        "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
        "@googletest//:gtest",
        "//:rel_parser"
    ],
)
