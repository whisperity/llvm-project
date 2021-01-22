#!/bin/bash

# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	autoconf \
	automake \
	libtool \
	curl \
	unzip

echo "Cloning project..."
git clone http://github.com/protocolbuffers/protobuf.git 

pushd protobuf
git checkout v3.11.2
git submodule update --init --recursive

echo "Generating build configuration..."
./autogen.sh
./configure

echo "Executing and logging build..."
CodeChecker log -b "make -j$(nproc)" -o "./compile_commands.json"
popd

echo "Done."

