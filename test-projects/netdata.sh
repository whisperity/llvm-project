#!/bin/bash

# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	automake \
	autoconf \
	pkg-config \
	gettext \
	uuid-dev \
	zlib1g-dev

echo "Cloning project..."
git clone http://github.com/netdata/netdata.git 

pushd netdata
git checkout v1.19.0

echo "Executing and logging build..."
CodeChecker log -b "./netdata-installer.sh --install $(readlink -f "./temp-install") --dont-wait --dont-start-it" -o "./compile_commands.json"
popd

echo "Done."

