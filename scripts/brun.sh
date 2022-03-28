#!/bin/bash
set -e; set -o pipefail

# Builds and runs the project with supplied arguments.

# Ref: https://stackoverflow.com/a/246128
script_dir=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)
build_dir="$script_dir/../build"

"$script_dir"/build.sh
"$build_dir"/wordle $@
