cmake_minimum_required(VERSION 3.13.0)

find_package(Boost 1.78 REQUIRED)

project(wordle CXX)
include_directories(include)

add_executable(wordle
    src/helpers.cc
    src/io.cc
    src/main.cc
    src/solver/core.cc
    src/solver/logging.cc
)
target_compile_options(wordle
    PUBLIC -g -Ofast -flto -march=native -Wall -Wextra --std=c++20
)
