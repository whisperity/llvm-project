#!/bin/bash

echo
echo "This script might take a very, very long time!"
echo
echo

echo "Running analysis with analyser at"
which clang-tidy

pushd Analysis-Configuration

./run-aa.sh "http://localhost:8001/AdjacentParams"

popd # Analysis-Configuration

echo "Done."

mv ./Reports-AA ./Reports-Matches

