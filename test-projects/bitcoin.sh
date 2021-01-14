#!/bin/bash

# Install some necessary requirements to make the project build.
sudo add-apt-repository --yes ppa:bitcoin/bitcoin
sudo apt update
sudo apt install --no-install-recommends --yes \
	automake \
	libtool \
	pkg-config \
	libdb4.8-dev libdb4.8++-dev \
	libboost-all-dev \
	libevent-dev
sudo apt install --yes \
	qtbase5-dev qttools5-dev

echo "Cloning project..."
git clone http://github.com/bitcoin/bitcoin.git 

pushd bitcoin
git checkout v0.19.0.1

echo "Generating build configuration..."
./autogen.sh
./configure

echo "Executing and logging build..."
CodeChecker log -b "make -j$(nproc)" -o "./compile_commands.json"
popd

echo "Done."

