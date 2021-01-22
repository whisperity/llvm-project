#!/bin/bash

# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	automake \
	autoconf \
	libtool

echo "Cloning project..."
git clone http://github.com/apache/xerces-c.git 

pushd xerces-c
git checkout v3.2.2

echo "Generating build configuration..."
./reconf
./configure

echo "Executing and logging build..."
CodeChecker log -b "make -j$(nproc)" -o "./compile_commands.json"
popd

echo "Done."

