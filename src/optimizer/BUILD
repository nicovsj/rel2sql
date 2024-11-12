cc_library(
    name = "optimizer",
    srcs = [
        "constant_optimizer.cc",
        "cte_optimizer.cc",
        "flattener_optimizer.cc",
        "self_join_optimizer.cc",
    ],
    hdrs = [
        "base_optimizer.h",
        "constant_optimizer.h",
        "cte_optimizer.h",
        "flattener_optimizer.h",
        "optimizer.h",
        "replacers.h",
        "self_join_optimizer.h",
    ],
    include_prefix = "optimizer",
    visibility = ["//visibility:public"],
    deps = [
        "//src/structs",
        "//src/utils",
        "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
        "@fmt",
    ],
)
