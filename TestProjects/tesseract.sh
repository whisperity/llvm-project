#!/bin/bash

# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	automake \
	libtool \
	pkg-config \
	libleptonica-dev \
	libpng-dev \
	libjpeg8-dev \
	libtiff5-dev \
	zlib1g-dev

echo "Cloning project..."
git clone http://github.com/tesseract-ocr/tesseract.git 

pushd tesseract
git checkout 4.1.0

echo "Generating build configuration..."
./autogen.sh
./configure

echo "Executing and logging build..."
CodeChecker log -b "make -j$(nproc)" -o "./compile_commands.json"
popd

echo "Done."

