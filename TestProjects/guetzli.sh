#!/bin/bash

# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	pkg-config \
	libpng-dev

echo "Cloning project..."
git clone http://github.com/google/guetzli.git 

pushd guetzli
git checkout v1.0.1

echo "Executing and logging build..."
CodeChecker log -b "make -j$(nproc)" -o "./compile_commands.json"
popd

echo "Done."

