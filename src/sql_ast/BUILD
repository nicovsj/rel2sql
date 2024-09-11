cc_library(
    name = "sql_ast",
    srcs = [
        "expr_visitor.cc",
        "opt_visitor.cc",
        "sql_ast.cc",
    ],
    hdrs = [
        "const_replacer.h",
        "expr_visitor.h",
        "opt_visitor.h",
        "sql_ast.h",
    ],
    include_prefix = "sql_ast",
    visibility = ["//visibility:public"],
    deps = [
        "//src/utils",
        "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
        "@fmt",
    ],
)
