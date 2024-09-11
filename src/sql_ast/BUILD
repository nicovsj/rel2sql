cc_library(
    name = "sql_ast",
    srcs = [
        "sql_ast.cc",
    ],
    hdrs = [
        "sql_ast.h",
        "expr_visitor.h",
    ],
    visibility = ["//visibility:public"],
    include_prefix = "sql_ast",
    deps = [
        "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
        "@fmt",
        "//src/utils:utils",
    ],
)
