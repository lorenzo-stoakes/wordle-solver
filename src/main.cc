#include "io.hh"
#include "solver.hh"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

using steady_clock_t = std::chrono::steady_clock;

// Determines how many guesses we make at each node in the decision
// tree. Heuristically 8 seems to give good results.
static constexpr int PRUNE_LIMIT = 8;

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " "
                  << "valid_guesses_path solutions_path [target_solution]"
                  << std::endl;

        return EXIT_FAILURE;
    }

    std::vector<std::string> valid_guesses, solutions;

    try {
        valid_guesses = wordle::read_lines(argv[1]);
        solutions = wordle::read_lines(argv[2]);
    } catch (const std::runtime_error& err) {
        std::cerr << "error: " << err.what() << std::endl;
        return EXIT_FAILURE;
    }

    try {
        // As per the spec -- `Any solution word can also be used as a valid guess.'
        wordle::combine_lines(valid_guesses, solutions);

        wordle::solver solv(valid_guesses, solutions);

        const auto begin = steady_clock_t::now();
        const wordle::results res = solv.solve(PRUNE_LIMIT);
        const auto end = steady_clock_t::now();

        if (argc == 4) {
            solv.print_tree(res, valid_guesses, solutions, argv[3]);
        } else {
            solv.print_tree(res, valid_guesses, solutions);

            std::cout << std::endl << "--- stats ---" << std::endl;
            solv.print_stats(res);
            std::cout << "-------------" << std::endl << std::endl;

            const auto time_taken_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
            std::cout << "Took " << time_taken_ms << " ms" << std::endl;
        }
    } catch (const std::runtime_error& err) {
        std::cerr << "error: " << err.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
