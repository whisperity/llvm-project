#!/bin/bash

echo "Cloning project..."
git clone http://github.com/antirez/redis.git 

pushd redis
git checkout 5.0.7

echo "Executing and logging build..."
CodeChecker log -b "make MALLOC=libc -j$(nproc)" -o "./compile_commands.json"
popd

echo "Done."

