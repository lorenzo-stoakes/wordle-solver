#include "helpers.hh"

#include <string>
#include <unordered_set>
#include <vector>

namespace wordle
{
void combine_lines(std::vector<std::string>& destination,
                   std::vector<std::string>& source)
{
    std::unordered_set existing_set(destination.begin(), destination.end());

    for (const std::string& str : source) {
        if (!existing_set.contains(str)) {
            destination.push_back(str);
            existing_set.insert(str);
        }
    }
}
} // namespace wordle
