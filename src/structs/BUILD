cc_library(
    name = "structs",
    srcs = [
        "expr_visitor.cc",
        "sql_ast.cc",
    ],
    hdrs = [
        "expr_visitor.h",
        "sql_ast.h",
        "extended_ast.h",
    ],
    include_prefix = "structs",
    visibility = ["//visibility:public"],
    deps = [
        "//src/utils",
        "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
        "@fmt",
    ],
)
