#!/bin/bash

# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	python3

echo "Cloning project..."
git clone http://github.com/Z3Prover/z3.git

pushd z3
git checkout z3-4.8.9

echo "Generating build configuration..."
mkdir Build
pushd Build
cmake \
	-G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	..

echo "Executing and logging build..."
TERM=dumb CodeChecker log -b "cmake --build . -- -j$(nproc)" -o "./compile_commands.json"
mv ./compile_commands.json ..
popd # z3/Build
popd # z3

echo "Done."

