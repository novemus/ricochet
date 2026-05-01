#!/bin/bash

set -e

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
make -j$(nproc)

echo "Build completed successfully"
