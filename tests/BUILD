load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

CORPUS_V1_FILES = glob(
    ["generator/corpus/v1/**"],
    allow_empty = True,
)

cc_library(
    name = "test_common",
    srcs = [
        "test_common.h",
    ],
    deps = [
        "//:rel2sql",
    ],
)

cc_library(
    name = "duckdb_exec",
    testonly = True,
    srcs = ["duckdb_exec.cc"],
    hdrs = ["duckdb_exec.h"],
    includes = ["."],
    deps = [
        ":duckdb_session",
        "//:rel2sql",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "duckdb_session",
    testonly = True,
    srcs = ["generator/duckdb_session.cc"],
    hdrs = ["generator/duckdb_session.h"],
    includes = ["."],
    deps = [
        "//:rel2sql",
        "//third_party/duckdb:duckdb",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "corpus_case_config",
    testonly = True,
    srcs = ["generator/corpus_case_config.cc"],
    hdrs = ["generator/corpus_case_config.h"],
    includes = ["."],
    deps = [
        ":rel_program_generator",
        ":test_common",
        "//:rel2sql",
    ],
)

cc_library(
    name = "corpus",
    testonly = True,
    srcs = ["generator/corpus.cc"],
    hdrs = [
        "generator/corpus.h",
        "generator/corpus_version.h",
    ],
    includes = ["."],
    deps = [
        ":rel_program_generator",
        ":test_common",
        "//:rel2sql",
    ],
)

cc_library(
    name = "rel_program_generator",
    testonly = True,
    srcs = [
        "generator/data_fixture.cc",
        "generator/raicode_paths.cc",
        "generator/rel_engine.cc",
        "generator/rel_engine_client.cc",
        "generator/rel_engine_json.cc",
        "generator/rel_program_generator.cc",
        "generator/result_set.cc",
        "generator/sql_executor.cc",
    ],
    hdrs = [
        "generator/data_fixture.h",
        "generator/raicode_paths.h",
        "generator/rel_engine.h",
        "generator/rel_engine_client.h",
        "generator/rel_engine_json.h",
        "generator/rel_program_generator.h",
        "generator/result_set.h",
        "generator/rng.h",
        "generator/sql_executor.h",
    ],
    data = [
        "generator/run_rel_engine_server.jl",
        "generator/run_rel_program.jl",
        "generator/rel_engine_core.jl",
    ],
    includes = ["."],
    deps = [
        ":duckdb_session",
        ":test_common",
        "//:rel2sql",
        "//third_party/duckdb:duckdb",
    ],
)

cc_binary(
    name = "print_generated_case",
    testonly = True,
    srcs = ["generator/print_generated_case.cc"],
    tags = ["manual"],
    deps = [
        ":corpus_case_config",
        ":rel_program_generator",
        ":test_common",
        "//:rel2sql",
    ],
)

cc_binary(
    name = "repro_mismatch",
    testonly = True,
    srcs = ["generator/repro_mismatch.cc"],
    tags = ["manual"],
    deps = [
        ":corpus_case_config",
        ":rel_program_generator",
        ":test_common",
        "//:rel2sql",
    ],
)

cc_binary(
    name = "validate_corpus",
    testonly = True,
    srcs = ["generator/validate_corpus.cc"],
    tags = ["manual"],
    deps = [":corpus"],
)

cc_binary(
    name = "build_corpus",
    testonly = True,
    srcs = ["generator/build_corpus.cc"],
    tags = [
        "manual",
        "requires-raicode",
    ],
    deps = [
        ":corpus",
        ":corpus_case_config",
        ":rel_program_generator",
        ":test_common",
        "//:rel2sql",
    ],
)

cc_binary(
    name = "retry_mismatches",
    testonly = True,
    srcs = ["generator/retry_mismatches.cc"],
    tags = [
        "manual",
        "requires-raicode",
    ],
    deps = [
        ":corpus",
        ":corpus_case_config",
        ":rel_program_generator",
        ":test_common",
        "//:rel2sql",
    ],
)

cc_test(
    name = "test_corpus_correctness",
    srcs = ["test_corpus_correctness.cc"],
    data = CORPUS_V1_FILES,
    tags = ["manual"],
    deps = [
        ":corpus",
        ":rel_program_generator",
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_generated_correctness",
    srcs = ["test_generated_correctness.cc"],
    deps = [
        ":corpus_case_config",
        ":rel_program_generator",
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_generated_correctness_raicode",
    srcs = ["test_generated_correctness_raicode.cc"],
    data = [
        "generator/run_rel_program.jl",
        "generator/rel_engine_core.jl",
        "generator/run_rel_engine_server.jl",
    ],
    tags = [
        "manual",
        "requires-raicode",
    ],
    deps = [
        ":rel_program_generator",
        ":test_common",
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
        ":duckdb_exec",
        ":test_common",
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
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_safety",
    srcs = [
        "test_safety.cc",
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
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_sql_parser",
    srcs = [
        "test_sql_parser.cc",
    ],
    deps = [
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_rel_ast",
    srcs = [
        "test_rel_ast.cc",
    ],
    deps = [
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_sql_parser_roundtrip",
    srcs = [
        "test_sql_parser_roundtrip.cc",
    ],
    deps = [
        ":test_common",
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "test_rewriter",
    srcs = [
        "test_rewriter.cc",
    ],
    deps = [
        "//:rel2sql",
        "@googletest//:gtest_main",
    ],
)
