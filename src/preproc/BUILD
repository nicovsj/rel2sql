cc_library(
    name = "preproc",
    srcs = glob(["*.cc"]),
    hdrs = glob(["*.h"]),
    include_prefix = "preproc",
    visibility = ["//visibility:public"],
    deps = [
        "//src/structs",
        "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
        "@googletest//:gtest",
        "//:rel_parser"
    ],
)
