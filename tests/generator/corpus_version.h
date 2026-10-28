#ifndef REL2SQL_TESTS_GENERATOR_CORPUS_VERSION_H_
#define REL2SQL_TESTS_GENERATOR_CORPUS_VERSION_H_

namespace rel2sql::generator {

// Bump when rel_program_generator, data_fixture, or profile presets change; then regen corpus.
inline constexpr const char* kCorpusGeneratorFingerprint = "rel2sql-generator-v1-20250616c";

// Corpus v2: dependency graphs of up to three defs, variants per slot, every outcome recorded.
inline constexpr const char* kCorpusV2GeneratorFingerprint = "rel2sql-generator-v2-20261027";

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_CORPUS_VERSION_H_
