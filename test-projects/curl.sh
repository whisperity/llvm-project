#!/bin/bash

# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	automake \
	libtool 

echo "Cloning project..."
git clone http://github.com/curl/curl.git 

pushd curl
git checkout curl-7_67_0 

echo "Generating build configuration..."
./buildconf
./configure

echo "Executing and logging build..."
CodeChecker log -b "make -j$(nproc)" -o "./compile_commands.json"
popd

echo "Done."

