#include "solver.hh"

#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
// Helper function to reverse-lookup the solution index for a guess index which
// is also a solution (we only use this in a non time-critical path.)
//
// O(n) time complexity.
size_t guess_to_solution_index(size_t guess_index,
                               const std::vector<std::string>& valid_guesses,
                               const std::vector<std::string>& solutions)
{
    const auto iter = std::ranges::find(solutions, valid_guesses[guess_index]);
    return static_cast<size_t>(std::distance(solutions.begin(), iter));
}
} // namespace

namespace wordle
{
// No target solution (print entire tree).
void solver::print_tree(const results& res, const std::vector<std::string>& valid_guesses,
                        const std::vector<std::string>& solutions)
{
    print_tree(res.head, valid_guesses, solutions, -1);
}

// Target solution specified (print only that part of the tree traversal).
void solver::print_tree(const results& res, const std::vector<std::string>& valid_guesses,
                        const std::vector<std::string>& solutions,
                        const std::string& target_solution)
{
    const auto iter = std::ranges::find(solutions, target_solution);

    if (iter == solutions.cend())
        throw std::runtime_error(std::string("Solution list does not contain '") +
                                 target_solution + "'");

    const auto target_solution_index =
        static_cast<ssize_t>(std::distance(solutions.begin(), iter));

    print_tree(res.head, valid_guesses, solutions, target_solution_index);
}

void solver::print_stats(const results& res) const
{
    tree_stats stats = {};
    get_stats(res.head, stats, 0);

    double sum = 0;
    int count = 0;

    for (int i = 0; i < NUM_ALLOWED_GUESSES; i++) {
        std::cout << i + 1 << " : " << stats.counts[i] << std::endl;
        sum += (i + 1) * stats.counts[i];
        count += stats.counts[i];
    }

    std::cout << "x : " << m_num_solutions - count << std::endl;
    std::cout << "av: " << (sum / count) << std::endl;
}

void solver::get_stats(const node* tree, tree_stats& stats, int depth)
{
    if (depth > NUM_ALLOWED_GUESSES - 1)
        return;

    if (tree->is_leaf)
        stats.counts[depth]++;

    for (const node* child : tree->children) {
        get_stats(child, stats, depth + 1);
    }

    if (depth < NUM_ALLOWED_GUESSES - 1)
        stats.counts[depth + 1] += tree->leaves.size();
}

void solver::extract_tree_stacks(
    const node* subtree,
    std::unordered_map<size_t, std::vector<size_t>>& stacks_by_solution,
    std::vector<size_t>& guesses_so_far,
    const std::vector<std::string>& valid_guesses,
    const std::vector<std::string>& solutions)
{
    if (subtree->is_leaf) {
        const size_t solution_index = guess_to_solution_index(subtree->guess_index,
                                                              valid_guesses,
                                                              solutions);

        stacks_by_solution[solution_index] = guesses_so_far;
    }

    guesses_so_far.push_back(subtree->guess_index);

    for (const size_t solution_index : subtree->leaves) {
        stacks_by_solution[solution_index] = guesses_so_far;
    }

    for (const node* child : subtree->children) {
        extract_tree_stacks(child,
                            stacks_by_solution,
                            guesses_so_far,
                            valid_guesses,
                            solutions);
    }

    guesses_so_far.pop_back();
}

std::unordered_map<size_t, std::vector<size_t>> solver::extract_tree_stacks(
    const node* subtree,
    const std::vector<std::string>& valid_guesses,
    const std::vector<std::string>& solutions)
{
    std::unordered_map<size_t, std::vector<size_t>> ret;
    std::vector<size_t> guesses_so_far;

    extract_tree_stacks(subtree, ret, guesses_so_far, valid_guesses, solutions);

    return ret;
}

void solver::print_tree(const node* tree, const std::vector<std::string>& valid_guesses,
                        const std::vector<std::string>& solutions,
                        ssize_t target_solution_index) const
{
    auto print_tree_stack = [&] (size_t solution_index, const std::vector<size_t>& guess_indexes) {
        for (const size_t guess_index : guess_indexes) {
            std::cout << valid_guesses[guess_index] << " ";

            const match_val_t match = lookup_match(guess_index, solution_index);
            std::cout << m_match_strings[match] << " ";
        }

        std::cout << solutions[solution_index] << std::endl;
    };

    const auto tree_stacks = extract_tree_stacks(tree, valid_guesses, solutions);

    if (target_solution_index >= 0) {
        const auto iter = tree_stacks.find(target_solution_index);
        if (iter == tree_stacks.cend())
            throw std::runtime_error("Invalid tree state!");

        print_tree_stack(target_solution_index, iter->second);
        return;
    }

    // Sort guesses by number of matches, guesses, and matches.
    std::vector<std::pair<size_t, std::vector<size_t>>> pairs(
        tree_stacks.begin(), tree_stacks.end());
    std::ranges::sort(pairs, {}, [this] (auto& pair) {
        auto x = pair.second
            | std::views::transform([this,
                                     solution_index=pair.first,
                                     num_guesses=pair.second.size()] (size_t guess_index) {
                return (num_guesses << 32)
                    | (guess_index << 11)
                    | lookup_match(guess_index, solution_index);
            });

        // not efficient but for the purposes of logging it isn't a huge issue.
        return std::vector(x.begin(), x.end());
    });

    for (const auto& [ solution_index, guess_indexes ] : pairs) {
        print_tree_stack(solution_index, guess_indexes);
    }
}
} // namespace wordle
