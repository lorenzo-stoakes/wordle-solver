#pragma once

#include "helpers.hh"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>
#include "unistd.h"

namespace wordle
{
// Forward reference to results wrapper object returned to user.
struct results;

// Contains state for solving wordle problems with the specified valid_guesses and
// solutions.
class solver final
{
public:
    // We represent distinct matches between guesses and solutions using an
    // 8-bit value which has distinct state for each individual match.
    using match_val_t = uint8_t;

    static constexpr int NUM_WORD_LETTERS = 5;
    static constexpr int NUM_ALLOWED_GUESSES = 6;

    // There are 3 distinct states for each letter - green, yellow or
    // grey. We therefore have 3^5 unique states (for 5 letters per word)
    // which is within the bounds of an 8-bit value.
    static constexpr match_val_t NUM_MATCH_VALS = raise_power(3, NUM_WORD_LETTERS);
    static constexpr match_val_t MAX_MATCH_VAL = static_cast<match_val_t>(-1);
    static constexpr match_val_t ALL_GREENS_MATCH = NUM_MATCH_VALS - 1;
    static_assert(NUM_MATCH_VALS < MAX_MATCH_VAL);

    // Represents a node in the decision tree.
    struct node
    {
        // We represent words by indexes into an array. This is the guess
        // associated with this node.
        size_t guess_index = 0;

        // We keep track of the cumulative sum of the depths of all child nodes
        // from here...
        int total_depth = 0;
        // ...we also track the total number of problems solved from this node
        // allowing us to determine the critical 'average number of guesses'
        // metric.
        int solved_count = 0;

        // This is the minimum depth from this node to a solution. If this
        // exceeds maximum number of permitted guesses then we must elide this
        // whole subtree.
        int min_depth = 0;

        // Is this node itself a leaf (i.e. represents a solution)?
        bool is_leaf = false;

        // Child nodes.
        std::vector<node *> children;

        // We store _known_ leaf nodes as solution index values for efficiency.
        std::vector<size_t> leaves;

        // Determine the average number of guesses it took to get to a solution
        // from this node. A KEY metric.
        double average_num_guesses() const
        {
            return solved_count == 0 ? 0
                : static_cast<double>(total_depth) / solved_count;
        }
    };

    // The solver object is constructed based on valid guess and solution arrays
    // - all internal reference to these is by indexes into these arrays.
    solver(const std::vector<std::string>& valid_guesses,
           const std::vector<std::string>& solutions)
        : m_num_valid_guesses{valid_guesses.size()}
        , m_num_solutions{solutions.size()}
        , m_match_vals{std::vector<match_val_t>(m_num_valid_guesses * m_num_solutions)}
        , m_match_strings{std::vector<std::string>(NUM_MATCH_VALS)}
        , m_prune_limit{0}
          // Double the number of available cores works well to account
          // for thread lifetime.
        , m_max_threads{::sysconf(::_SC_NPROCESSORS_ONLN) * 2}
        , m_num_threads{0}
        {
            check_guesses_solutions(valid_guesses, solutions);

            // We pre-calculate all matches (e.g. ..G.y, .GGy.y, etc.) between
            // availble guesses and solutions. This is a critical memoisation.
            init_match_vals(valid_guesses, solutions);
        }

    // The key entry point for solving wordle -- solves it with a `prune limit'
    // which specifies how many of the most promising child nodes are examined
    // at each node in the decision tree. One can maintain surprsiingly
    // excellent results even with a relatively low value.
    //
    // Note that this call is NOT thread safe.
    results solve(int prune_limit);

    // Print the entire decision tree to standard out in a form useful for a
    // wordle player to develop a strategy on.
    void print_tree(const results& res,
                    const std::vector<std::string>& valid_guesses,
                    const std::vector<std::string>& solutions);

    // Print the guesses the strategy would use for the specified solution to
    // standard out.
    void print_tree(const results& res,
                    const std::vector<std::string>& valid_guesses,
                    const std::vector<std::string>& solutions,
                    const std::string& target_solution);

    // Print useful statistics indicating guess count frequency and average
    // guess count to standard out.
    void print_stats(const results& res) const;

private:
    // The memoisation map we use -- mapping from a solution index set to the
    // node pointer associated with it.
    using memo_t = std::unordered_map<std::vector<size_t>, node *>;

    // Represents decision tree statistics.
    struct tree_stats
    {
        int counts[NUM_ALLOWED_GUESSES];
    };

    // Keep track of the number of valid guesses and solutions.
    const size_t m_num_valid_guesses;
    const size_t m_num_solutions;
    // Represents a matrix containing distinct values for each guess/solution
    // match.
    std::vector<match_val_t> m_match_vals;
    // We cache strings which represent
    std::vector<std::string> m_match_strings;
    // The current prune limit.
    int m_prune_limit;
    // The current memoisation state.
    memo_t m_memo;

    // Thread state in order to keep reasonable thread count.
    const int64_t m_max_threads;
    std::atomic<int> m_num_threads;
    mutable std::mutex m_memo_mutex;

    // Lookup the match between the specified guess and solution.
    match_val_t lookup_match(size_t guess_index, size_t solution_index) const
    {
        const size_t index = guess_index * m_num_solutions + solution_index;
        return m_match_vals[index];
    }

    // The 'heart' of the solver, called recursively to obtain reasonably
    // optimal guesses at each point.
    node* solve(const std::vector<size_t>& solution_indexes, int depth);

    // Traverse all possible matches for the specified available solutions and
    // guess.
    void traverse_matches(node& subtree, size_t guess_index,
                          const std::vector<size_t>& solution_indexes,
                          int depth);

    // Traverse a specific guess/match combination (with associated possible
    // availble solutions) recursing into solve() and updating the `subtree`
    // accordingly.
    bool traverse_match(node& subtree, size_t guess_index, int depth,
                        const std::vector<size_t>& avail_solutions);

    // Mark a decision tree node solved and update statistics to that effect.
    void mark_solved(node& subtree, size_t guess_index, size_t solution_index);

    // Get the guess indexes for guesses with the mimimum average solutions per
    // unique match metric up to the prune limit.
    std::vector<std::pair<double, size_t>> get_best_unique_match_guesses(
        const std::vector<size_t>& solution_indexes) const;

    // Determine the average number of solutions per unique match for a specific
    // guess, a KEY metric for cutting down on how many guesses need to be considered.
    double avg_solutions_per_unique_match(const std::vector<size_t>& solution_indexes,
                                          size_t guess_index) const;

    // Get sets of solution indexes by unique match value.
    std::vector<std::vector<size_t>> get_solutions_by_match(
        size_t guess_index,
        const std::vector<size_t>& solution_indexes) const;

    // Calculate the unique value associated with a specific match e.g. '.G..y'
    // or 'GGy.y'. Additionally updates match value strings for log output.
    match_val_t calc_match_val(const std::string& guess, const std::string& solution);

    // Initialise the match value matrix.
    void init_match_vals(const std::vector<std::string>& valid_guesses,
                         const std::vector<std::string>& solutions);

    // Lookup a memoised decision node. Since we parameterise by solution index
    // set, this is the key used.
    node* lookup_memo(const std::vector<size_t>& key) const;

    // Insert a tree node into the memoisation set.
    void set_memo(const std::vector<size_t>& key, node* val);

    // Check to ensure valid guesses and solutions are of the correct length and
    // to ensure all solutions are also guesses too.
    void check_guesses_solutions(const std::vector<std::string>& valid_guesses,
                                 const std::vector<std::string>& solutions) const;

    // Get useful statistics on guess frequency to be output by
    // .print_stats(). Called recursively.
    static void get_stats(const node* tree, tree_stats& stats, int depth);

    static void extract_tree_stacks(
        const node* subtree,
        std::unordered_map<size_t, std::vector<size_t>>& stacks_by_solution,
        std::vector<size_t>& guesses_so_far,
        const std::vector<std::string>& valid_guesses,
        const std::vector<std::string>& solutions);

    static std::unordered_map<size_t, std::vector<size_t>> extract_tree_stacks(
        const node* tree,
        const std::vector<std::string>& valid_guesses,
        const std::vector<std::string>& solutions);

    // Print out the decision tree either in its entirety (if
    // target_solution_index < 0) or only the entry for the specified target
    // solution.
    void print_tree(const node* tree, const std::vector<std::string>& valid_guesses,
                    const std::vector<std::string>& solutions,
                    ssize_t target_solution_index) const;
}; // class solver

// Represents the results of a wordle analysis.
struct results final
{
    results() : head{nullptr}
    {
    }

    results(const solver::node* head) : head{head}
    {
    }

    const solver::node* head;

    // We specifically place the tree dtor in the results object in order that
    // we don't spuriously trigger incorrect frees when moving node objects
    // around. All nodes here will definitely be allocated as part of a dynamic
    // array.
    ~results()
     {
         delete_tree(head);
     }

private:
    // Free up all manually allocated memory in tree and child nodes.
    void delete_tree(const solver::node* tree);
};
} // namespace wordle
