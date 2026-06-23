#ifndef REL2SQL_TESTS_GENERATOR_CORPUS_VERSION_H_
#define REL2SQL_TESTS_GENERATOR_CORPUS_VERSION_H_

namespace rel2sql::generator {

// Bump when rel_program_generator, data_fixture, or profile presets change; then regen corpus.
inline constexpr const char* kCorpusGeneratorFingerprint = "rel2sql-generator-v1-20250625b";

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_CORPUS_VERSION_H_
