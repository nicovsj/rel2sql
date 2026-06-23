#ifndef REL2SQL_TESTS_GENERATOR_RNG_H_
#define REL2SQL_TESTS_GENERATOR_RNG_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rel2sql::generator {

// Deterministic PRNG for reproducible program generation.
class Rng {
 public:
  explicit Rng(uint64_t seed) : state_(seed) {}

  uint64_t Next() {
    uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }

  // Uniform in [0, upper).
  size_t Uniform(size_t upper) {
    if (upper <= 1) return 0;
    return static_cast<size_t>(Next() % upper);
  }

  // Uniform in [lower, upper].
  int UniformInt(int lower, int upper) {
    if (upper <= lower) return lower;
    return lower + static_cast<int>(Uniform(static_cast<size_t>(upper - lower + 1)));
  }

  bool CoinFlip() { return (Next() & 1) == 0; }

  template <typename T>
  void Shuffle(std::vector<T>& values) {
    for (size_t i = values.size(); i > 1; --i) {
      size_t j = Uniform(i);
      std::swap(values[j], values[i - 1]);
    }
  }

  double NextDouble() {
    // [0, 1)
    return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0);
  }

 private:
  uint64_t state_;
};

inline uint64_t MixSeed(uint64_t seed, size_t program_index, uint64_t attempt = 0) {
  uint64_t x = seed;
  x ^= static_cast<uint64_t>(program_index) + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2);
  x ^= attempt + 0x94d049bb133111ebULL + (x << 6) + (x >> 2);
  return x;
}

// Seed for program generation; node_budget is mixed in so budget sweeps explore distinct shapes.
inline uint64_t ProgramGenerationSeed(uint64_t seed, size_t program_index, size_t node_budget, uint64_t attempt = 0) {
  uint64_t x = MixSeed(seed, program_index, attempt);
  x ^= static_cast<uint64_t>(node_budget) + 0x517cc1b727220a95ULL + (x << 6) + (x >> 2);
  return x;
}

}  // namespace rel2sql::generator

#endif  // REL2SQL_TESTS_GENERATOR_RNG_H_
