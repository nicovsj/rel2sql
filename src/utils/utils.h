// cspell: ignore utls
#ifndef UTILS_H
#define UTILS_H

#include <functional>
#include <unordered_set>

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

template <typename T>
std::unordered_set<T> IntersectSets(const std::vector<std::unordered_set<T>>& sets) {
  if (sets.empty()) return {};

  // Start with the first set as the base for intersection
  std::unordered_set<T> intersection = sets[0];

  // Iterate through the rest of the sets
  for (size_t i = 1; i < sets.size(); ++i) {
    std::unordered_set<T> currentIntersection;

    // Iterate through the current intersection set
    for (const T& elem : intersection) {
      // If the element is in the next set, add it to the current intersection
      if (sets[i].count(elem)) {
        currentIntersection.insert(elem);
      }
    }

    // Update the intersection to be the current intersection
    intersection = std::move(currentIntersection);

    // If at any point the intersection becomes empty, break early
    if (intersection.empty()) {
      break;
    }
  }

  return intersection;
}

}  // namespace utl

#endif  // UTILS_H
