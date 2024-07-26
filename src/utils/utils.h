// cspell: ignore utls
#ifndef UTILS_H
#define UTILS_H

#include <functional>

namespace utl {

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};

template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

template <typename T>
inline std::size_t hash_combine(std::size_t seed, const T& value) {
  std::hash<T> hasher;
  seed ^= hasher(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  return seed;
}

template <typename It>
inline std::size_t hash_range(std::size_t seed, It first, It last) {
  for (; first != last; ++first) {
    seed = hash_combine(seed, *first);
  }

  return seed;
}

}  // namespace utl

#endif  // UTILS_H
