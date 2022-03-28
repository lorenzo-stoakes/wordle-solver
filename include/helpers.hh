#pragma once

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>
#include "boost/functional/hash.hpp"

// Permit hashing of vectors.
namespace std
{
template <typename T>
struct hash<std::vector<T>>
{
    size_t operator()(const std::vector<T>& vec) const
    {
        size_t seed = vec.size();
        for(auto& val : vec) {
            boost::hash_combine(seed, val);
        }
        return seed;
    }
};
} // namespace std

namespace wordle
{
// Compile-time function which recursively raises `value` to the power of
// `exponent`.
template<typename T>
static inline consteval T raise_power(T value, T exponent)
{
    return exponent <= 0 ? 1 : value * raise_power(value, exponent - 1);
}

// Wrapper function to obtain a reference to the minimum element in a range with
// the specified projection.
template<typename TRet, typename TRange, typename TProj>
static inline TRet& get_range_min(TRange range, TProj&& proj)
{
    return *std::ranges::min_element(range.begin(), range.end(), {},
                                     std::forward<TProj>(proj));
}

// Helper function that places lines from source to destination of they don't
// already exist there.
void combine_lines(std::vector<std::string>& destination,
                   std::vector<std::string>& source);
} // namespace wordle
