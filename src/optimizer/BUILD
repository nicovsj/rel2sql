cc_library(
    name = "optimizer",
    srcs = [
        "opt_visitor.cc"
    ],
    hdrs = [
        "replacers.h",
        "opt_visitor.h",
    ],
    include_prefix = "optimizer",
    visibility = ["//visibility:public"],
    deps = [
        "//src/utils",
        "//src/structs",
        "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
        "@fmt",
    ],
)
