#include "helpers.hh"
#include "solver.hh"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wordle
{
results solver::solve(int prune_limit)
{
    // We retain per-run state, and thus are not reentrant here.
    m_prune_limit = std::min(static_cast<size_t>(prune_limit), m_num_valid_guesses - 1);
    m_memo.clear();

    // We track currently feasible solutions through an array of indexes into
    // the solutions list provided.
    std::vector<size_t> solution_indexes(m_num_solutions);
    std::iota(solution_indexes.begin(), solution_indexes.end(), 0);

    return results(solve(solution_indexes, 0));
}

solver::node* solver::solve(const std::vector<size_t>& solution_indexes, int depth)
{
    auto within_depth = [depth] (node& subtree) {
        return depth + subtree.min_depth <= NUM_ALLOWED_GUESSES;
    };

    // If we have a memoised result simply return that.
    node* cached = lookup_memo(solution_indexes);
    if (cached != nullptr && within_depth(*cached))
        return cached;

    // Find the top `m_prune_limit` average number of solutions per unique match
    // (a unique match being e.g. G..yG or GG...). We rank matches that have a
    // lower average (i.e. narrow down the solutions more) as being more
    // favourable.
    const auto best_guesses = get_best_unique_match_guesses(solution_indexes);
    const int num_guesses = best_guesses.size();

    // Allocate new nodes. It is more efficient to dynamically allocate these
    // contiguously.
    node* subtrees = new node[num_guesses];

    // Traverse the decision tree further for the top candidates, spreading the
    // work over threads if appropriate.
    std::vector<std::thread> threads;
    for (int i = 0; i < num_guesses; i++) {
        const size_t guess_index = best_guesses[i].second;
        node* subtree = &subtrees[i];
        subtree->guess_index = guess_index;

        // Determine whether to lay the work off to a worker thread or not.
        if (m_num_threads >= m_max_threads || num_guesses == 1) {
            traverse_matches(*subtree, guess_index, solution_indexes, depth);
        } else {
            threads.push_back(std::thread([&] (node* subtree, size_t guess_index) {
                traverse_matches(*subtree, guess_index, solution_indexes, depth);
            }, subtree, guess_index));

            m_num_threads++;
        }
    }

    // Join all worker threads.
    for (auto& thr : threads) {
        thr.join();
        m_num_threads--;
    }

    // Rank the result based on a second hueristic -- AVERAGE NUMBER OF
    // GUESSES TO REACH A SOLUTION, the lower the better.
    auto filtered = std::span(subtrees, num_guesses) | std::views::filter(within_depth);
    // In unlikely case we have no nodes available within depth default to the
    // first.
    node& best = filtered.empty()
        ? subtrees[0]
        : get_range_min<node>(filtered, &node::average_num_guesses);

    // Always swap to the first entry in the array so we can delete[] the
    // subtree array on cleanup.
    std::swap(best, subtrees[0]);

    // We memoise based on the current subset of available solutions. Each node
    // is independent of its parents (i.e. independent of current depth
    // specified by the `depth' parameter) and since we are parameterised by
    // solution indexes this is an effective unique memoisation key.
    set_memo(solution_indexes, &subtrees[0]);

    return &subtrees[0];
}

void solver::traverse_matches(node& subtree, size_t guess_index,
                              const std::vector<size_t>& solution_indexes,
                              int depth)
{
    // Generate a map between match value and individual solution index sets for
    // all possible matches for each (guess, solution) pair.
    const auto& solutions_by_match = get_solutions_by_match(guess_index,
                                                            solution_indexes);

    // Traverse each match individually, using the available solutions for each
    // match to examine where to explore next.
    for (int match = 0; match < NUM_MATCH_VALS; match++) {
        const auto& avail_solutions = solutions_by_match[match];

        // If the minimum depth to a solution exceeds available guesses then we
        // should abort our traversal.
        if (!traverse_match(subtree, guess_index, depth, avail_solutions))
            break;
    }
}

bool solver::traverse_match(node& subtree, size_t guess_index, int depth,
                            const std::vector<size_t>& avail_solutions)
{
    // This node has no available solutions, abort (but carry on examining other
    // matches).
    if (avail_solutions.empty())
        return true;

    // Since there is only one avilable solution for this unique match it is
    // either us or 1 guess away.
    if (avail_solutions.size() == 1) {
        mark_solved(subtree, guess_index, avail_solutions[0]);
        return true;
    }

    // Recursively try further guesses from here.
    node* child = solve(avail_solutions, depth + 1);

    subtree.children.push_back(child);
    subtree.solved_count += child->solved_count;
    // A tricky one here -- every solution is at depth + 1 from the node at
    // which it was solved so we also add solved count to take this into
    // account.
    subtree.total_depth += child->solved_count + child->total_depth;

    // The minimum depth before we hit a solution from here.
    subtree.min_depth = std::min(subtree.min_depth, child->min_depth + 1);

    // If we have exceeded the number of allowable guesses, abort.
    return depth + subtree.min_depth <= NUM_ALLOWED_GUESSES;
}

void solver::mark_solved(node& subtree, size_t guess_index, size_t solution_index)
{
    subtree.solved_count++;
    // This node being a solution adds at least one to the total depth to a
    // solution.
    subtree.total_depth++;

    // We have two possibilities here -- either this node is a solution, or
    // there is a solution only 1 guess away. Determine which.
    if (lookup_match(guess_index, solution_index) == ALL_GREENS_MATCH) {
        subtree.is_leaf = true;
        // Our minimum depth may not yet be set, if so ensure it is minimally 1.
        subtree.min_depth = std::max(subtree.min_depth, 1);
    } else {
        // We choose to keep trivial leaf nodes like this in a separate array
        // for efficiency.
        subtree.leaves.push_back(solution_index);
        // We have already taken into account the depth traversal to get to this
        // node, so take into account the leaf as well.
        subtree.total_depth++;
        // Our minimum depth may not yet be set, if so ensure it is minimally 2.
        subtree.min_depth = std::max(subtree.min_depth, 2);
    }
}

std::vector<std::pair<double, size_t>>
solver::get_best_unique_match_guesses(const std::vector<size_t>& solution_indexes) const
{
    auto get_avg = [&] (size_t guess_index) {
        return avg_solutions_per_unique_match(solution_indexes, guess_index);
    };

    // We examine averages for _all_ valid guesses (see above for a description
    // of the average used).
    const auto avgs =
        std::views::iota((size_t)0, m_num_valid_guesses)
        | std::views::transform(get_avg);

    // Determine each guesses's average number of solutions per unique match.

    std::vector<std::pair<double, size_t>> ret;
    ret.reserve(m_num_valid_guesses);

    for (size_t guess_index = 0; guess_index < m_num_valid_guesses; guess_index++) {
        const double avg = avgs[guess_index];
        // If we hit a guess that has less than 1 solution per unique match it
        // is of such high value that we should abort our analysis and
        // immediately use this guess.
        if (avg < 1)
            return { { avg, guess_index } };

        ret.emplace_back(avg, guess_index);
    }

    // We only need to consider guesses up to the prune limit so apply a partial
    // sort here (should be something like a heap generated in O(m lg m) time
    // where m is the prune limit).
    std::ranges::partial_sort(ret,
                              ret.begin() + m_prune_limit + 1,
                              {},
                              [] (auto& pair) { return pair.first; });

    ret.resize(m_prune_limit);
    return ret;
}

double solver::avg_solutions_per_unique_match(const std::vector<size_t>& solution_indexes,
                                              size_t guess_index) const
{
    std::array<bool, NUM_MATCH_VALS> seen = {};

    // Count the average number of solutions per unique match. Since each unique
    // match is a different decision point and fewer solutions being possible
    // for each match narrows things down, we prefer to minimise this value.

    int num_unique_matches = 0;
    for (const size_t solution_index : solution_indexes) {
        const match_val_t match = lookup_match(guess_index, solution_index);

        if (!seen[match]) {
            num_unique_matches++;
            seen[match] = true;
        }
    }

    // We want to improve the score (i.e. lower it) when we see a
    // match to prefer it to a non-solution guess.
    const int num_solutions = seen[ALL_GREENS_MATCH]
        ? solution_indexes.size() - 1
        : solution_indexes.size();

    return static_cast<double>(num_solutions) / num_unique_matches;
}

std::vector<std::vector<size_t>>
solver::get_solutions_by_match(size_t guess_index,
                               const std::vector<size_t>& solution_indexes) const
{
    std::vector<std::vector<size_t>> ret(NUM_MATCH_VALS);

    // Aggregate solutions by unique match value.
    for (size_t solution_index : solution_indexes) {
        const match_val_t match_val = lookup_match(guess_index, solution_index);
        ret[match_val].push_back(solution_index);
    }

    return ret;
}

solver::match_val_t solver::calc_match_val(const std::string& guess, const std::string& solution)
{
    int match_val = 0;
    int mult = 1;
    std::array<bool, NUM_WORD_LETTERS> seen_solution = {};

    // We simultaneously update a string representation of this match.
    std::string match_string(NUM_WORD_LETTERS, '.');

    // We use a base-3 system to assign unique values to each match:
    //   0 - grey,   letter does not match any in solution.
    //   1 - yellow, letter matches solution letter but not in this position
    //       (NOTE: we consider only the first for duplicate letters).
    //   2 - green,  letter matches solution letter AND in the correct
    //               position.
    //
    // We therefore keep 3^num_word_letters (5 by default) state which needs
    // to fit into a byte. We track the current multiple of 3 via `mult`.
    for (size_t i = 0; i < NUM_WORD_LETTERS; i++, mult *= 3) {
        const char guess_chr = guess[i];

        // Green match?
        if (guess_chr == solution[i]) {
            match_val += 2 * mult;
            seen_solution[i] = true;
            match_string[i] = 'G';
            continue;
        }

        // Yellow match?
        for (size_t j = 0; j < NUM_WORD_LETTERS; j++) {
            // Green matches are not simultaneously yellow as
            // well. Additionally, we only count the first yellow in the case of
            // duplicate letters.
            if (seen_solution[j])
                continue;

            const char solution_chr = solution[j];

            // We check for yellows taking care to avoid the tricky situation of
            // finding a green before we have examined the guess character for
            // it.
            if (guess_chr == solution_chr && guess[j] != solution[j]) {
                match_val += mult;
                seen_solution[j] = true;
                match_string[i] = 'y';
                break;
            }
        }
    }

    m_match_strings[match_val] = match_string;
    return static_cast<match_val_t>(match_val);
}

void solver::init_match_vals(const std::vector<std::string>& valid_guesses,
                             const std::vector<std::string>& solutions)
{
    // Initialise the matrix of all possible unique match values.
    for (size_t i = 0; i < m_num_valid_guesses; i++) {
        for (size_t j = 0; j < m_num_solutions; j++) {
            const size_t index = i * m_num_solutions + j;
            m_match_vals[index] = calc_match_val(valid_guesses[i], solutions[j]);
        }
    }
}

solver::node* solver::lookup_memo(const std::vector<size_t>& key) const
{
    std::lock_guard guard(m_memo_mutex);

    auto memo_iter = m_memo.find(key);
    if (memo_iter == m_memo.end())
        return nullptr;

    return memo_iter->second;
}

void solver::set_memo(const std::vector<size_t>& key, node* val)
{
    std::lock_guard guard(m_memo_mutex);

    m_memo[key] = val;
}

void solver::check_guesses_solutions(const std::vector<std::string>& valid_guesses,
                                     const std::vector<std::string>& solutions) const
{
    if (valid_guesses.empty())
        throw std::runtime_error("Empty guesses");

    if (solutions.empty())
        throw std::runtime_error("Empty solutions");

    std::unordered_set guess_set(valid_guesses.begin(), valid_guesses.end());

    for (const std::string& solution : solutions) {
        if (solution.size() != NUM_WORD_LETTERS) {
            std::ostringstream oss;

            oss << "Solution '" << solution << " is of length " << solution.size()
                << ", expected " << NUM_WORD_LETTERS;
            throw std::runtime_error(oss.str());
        }

        if (!guess_set.contains(solution)) {
            std::ostringstream oss;

            oss << "Guess set does not contain solution '" << solution << "'"
                << " it is a requirement that input guesses contain all solutions.";
            throw std::runtime_error(oss.str());
        }
    }

    for (const std::string& guess : valid_guesses) {
        if (guess.size() != NUM_WORD_LETTERS) {
            std::ostringstream oss;

            oss << "Guess '" << guess << " is of length " << guess.size()
                << ", expected " << NUM_WORD_LETTERS;
            throw std::runtime_error(oss.str());
        }
    }
}

void results::delete_tree(const solver::node* tree)
{
    for (const solver::node* child : tree->children) {
        delete_tree(child);
    }

    // The implementation will have positioned all valid result tree nodes at
    // the front of the array which will make this work.
    delete[] tree;
}
} // namespace wordle
