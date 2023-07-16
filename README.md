# Wordle solver

This is an efficient tool for finding near-optimal solutions to the wordle
puzzle game. It is optimised to minimise the average number of guesses required
to reach a solution.

On my system for the default wordle word lists I am able to obtain an average of
~3.421 guesses per solution, with a maximum of 5 guesses required in around
1.5s. This is at a prune limit of 8 (8 guesses considered at each node in the
decision tree).

This is comparable to the top solvers on the [wordle
leaderboard](https://freshman.dev/wordle/#/leaderboard).

## Building

To build, simply invoke the build script. It assumes that the system has cmake
and boost 1.78 available.

```
scripts/build.sh
```

The binary will be generated in `build/wordle` but you can build and run
directly via:

```
scripts/brun.sh [parameters]
```

## Usage

The solver can be used in two different ways - to determine the most efficient
route to a _known_ solution, or to generate a decision tree to find the most
efficient route to _all_ known solutions.

For the quickest TL;DR route to an unknown solution - always guess `salet` first
then grep `example-strategy-output.txt` for the response and follow that path.

The binary itself can be used as follows:-

```
build/wordle valid_guesses_path solutions_path [target solution]
```

Each valid guess and solutions file must contain newline-separated word lists
each at the required length (by default 5 letters).

For convenience, the last known set of valid wordle guesses and solutions are
supplied in the repository in `wordle-allowed-guesses.txt` and
`wordle-answers-alphabetical.txt`. For convenience, you can use these via:-

```
scripts/solve.sh [target solution]
```

If the target solution is omitted the tool will generate the decision tree for
the input guesses and output a strategy output like:


```
salet ..... courd ..... nymph ..... fizzy yG..G jiffy
salet ..... courd ..... nymph ..... fizzy
salet ..... courd ..... nymph yy... kinky
salet ..... courd ..... nymph Gy... ninny
salet ..... courd ..... nymph yG... vying
salet ..... courd ..... nymph y.y.. minim
salet ..... courd ..... nymph .y.y. piggy
salet ..... courd ..... nymph yy.y. pinky
salet ..... courd ..... nymph .Gyy. pygmy
salet ..... courd ..... nymph .yGG. wimpy
salet ..... courd ..... nymph ....y whiff
```

The format consists of guesses from left to right connected by 'matches' --
these are the results wordle provides when you input a guess. A dot represents a
grey result (letter not in word), 'G' a green result (correct letter and
position) and 'y' (correct letter, incorrect position) a yellow result.

To use this to play wordle, start with the opening word (this is always the
same), then grep for '[opening word] [match]'

e.g. if the opening word was 'salet' and the match was ...y. then you would
(f)grep for 'salet ...y.'. The next guess to try is the word in the next column
and so on until you reach the solution which is the word in the rightmost
column.

## Architecture

The tool uses a [decision tree](https://en.wikipedia.org/wiki/Decision_tree) to
analyse the results of taking different guesses at each point. As this is a
combinatorial explosion (`|valid guesses| ^ |number allowed guesses|` which with
the default wordle word lists comes to 4,764,766,690,345,931,033,186,304
possible games) it:

1. Applies heuristics at each stage to try to explore the most promising guesses
   at each point and rate them.

2. Limits the number of guesses evaluated at each node in the tree.

The specific process used is as follows:-

1. Determine the best guesses to investigate -- ordering them by average
   solutions per unique match and examining the top N of those where N is the
   'prune limit', used to limit computation time. This metric is useful because
   it tells us how much we are 'narrowing things down' -- if a guess on average
   across all possible solutions results in fewer solutions then it is maximally
   useful.

2. For each of the top N guesses, examine every possible match value and
   associated feasible solutions were that match (e.g. .yyG.) to be seen. Then,
   recursively examine the available guesses at each match (i.e. jumping back to
   step 1 for each child node in the decision tree).

3. Finally, use a second heuristic to determine which of these nodes to select
   at each stage -- the one with the lowest _average number of guesses to reach
   a solution_. This minimises our guess count.
