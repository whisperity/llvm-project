#!/bin/bash

# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	gettext \
	libcurl4-openssl-dev \
	libssh2-1-dev \
	zlib1g-dev

echo "Cloning project..."
git clone http://github.com/git/git.git 

pushd git
git checkout v2.24.1

echo "Generating build configuration..."
./configure

echo "Executing and logging build..."
CodeChecker log -b "make -j$(nproc)" -o "./compile_commands.json"
popd

echo "Done."

