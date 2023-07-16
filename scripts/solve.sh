#!/bin/bash
set -e; set -o pipefail

# Ref: https://stackoverflow.com/a/246128
script_dir=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)
build_dir="$script_dir/../build"
ref_dir="$script_dir/.."

"$script_dir"/brun.sh "$ref_dir"/wordle-allowed-guesses.txt "$ref_dir"/wordle-answers-alphabetical.txt $1
