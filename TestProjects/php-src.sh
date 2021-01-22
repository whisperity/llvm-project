#!/bin/bash

# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	autoconf \
	pkg-config \
	bison \
	re2c \
	libxml2-dev \
	libsqlite3-dev

echo "Cloning project..."
git clone http://github.com/php/php-src.git 

pushd php-src
git checkout php-7.4.1

echo "Generating build configuration..."
./buildconf --force
./configure

echo "Executing and logging build..."
CodeChecker log -b "make -j$(nproc)" -o "./compile_commands.json"
popd

echo "Done."

