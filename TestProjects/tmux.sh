#!/bin/bash

# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	autoconf \
	automake \
	pkg-config \
	libevent-dev \
	bison \
	libncurses5-dev 

echo "Cloning project..."
git clone http://github.com/tmux/tmux.git 

pushd tmux
git checkout 3.0

echo "Generating build configuration..."
./autogen.sh
./configure

echo "Executing and logging build..."
CodeChecker log -b "make -j$(nproc)" -o "./compile_commands.json"
popd

echo "Done."

