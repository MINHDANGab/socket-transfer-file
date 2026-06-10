#!/bin/bash

set -e

echo "Cleaning old build..."

rm -rf build
mkdir -p build
cd build
cmake ..
cmake --build .

cp -r ../data .
