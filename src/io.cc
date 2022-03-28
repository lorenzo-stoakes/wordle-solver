#include "solver.hh"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wordle
{
std::vector<std::string> read_lines(const char* path)
{
    std::ifstream stream(path);
    if (!stream.is_open())
        throw std::runtime_error(std::string("Unable to open: ") + path);

    std::vector<std::string> ret;

    char buffer[solver::NUM_WORD_LETTERS + 1];
    while (stream.getline(buffer, sizeof(buffer))) {
        ret.push_back(buffer);
    }

    return ret;
}
} // namespace wordle
