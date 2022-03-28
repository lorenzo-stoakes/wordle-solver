#!/bin/bash
set -e; set -o pipefail

# Builds the project using maximum available cores.

# Ref: https://stackoverflow.com/a/246128
script_dir=$(cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd)
build_dir="$script_dir/../build"

mkdir -p "$build_dir"
cd "$build_dir"

cmake ..
num_cores=$(grep -c ^processor /proc/cpuinfo)
make -j $num_cores $@
