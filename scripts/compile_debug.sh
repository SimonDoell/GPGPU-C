#!/usr/bin/env bash
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build && cmake --build build
