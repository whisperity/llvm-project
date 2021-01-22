#!/bin/bash

# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	bison \
	flex \
	libreadline-dev

echo "Cloning project..."
git clone http://github.com/postgres/postgres.git 

pushd postgres
git checkout REL_12_1

echo "Generating build configuration..."
./configure

echo "Executing and logging build..."
CodeChecker log -b "make -j$(nproc)" -o "./compile_commands.json"
popd

echo "Done."

