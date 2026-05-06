#!/bin/bash
set -ex

cmake -S . -B build
cmake --build build

cmake -S tests -B build_tests
cmake --build build_tests
./build_tests/tests
