#!/usr/bin/env bash
set -euo pipefail

# Build a release cdylib for Linux x86_64
cargo build --release

# The output on Linux will be target/release/libskeet_example_plugin.so
echo "Built: target/release/libskeet_example_plugin.so"
