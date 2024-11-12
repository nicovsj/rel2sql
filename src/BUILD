cc_library(
    name = "translate",
    hdrs = ["translate.h", "sql_visitor.h"],
    srcs = ["sql_visitor.cc"],
    include_prefix = "rel2sql",
    strip_include_prefix = "",
    visibility = ["//visibility:public"],
    deps = [
        "//src/optimizer",
        "//src/structs",
        "//src/preproc",
        "//:rel_parser",
    ],
)
