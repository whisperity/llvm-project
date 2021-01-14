#!/bin/bash

echo "Cloning project..."
git clone http://github.com/opencv/opencv.git 

pushd opencv
git checkout 4.2.0

echo "Generating build configuration..."
mkdir Build
pushd Build
cmake ..

echo "Executing and logging build..."
CodeChecker log -b "cmake --build . -- -j$(nproc)" -o "./compile_commands.json"
mv ./compile_commands.json ..
popd # opencv/Build

popd # opencv

echo "Done."

