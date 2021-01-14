#!/bin/bash

echo "Cloning project..."
git clone http://github.com/llvm/llvm-project.git 

pushd llvm-project
git checkout llvmorg-9.0.0

echo "Generating build configuration..."
mkdir Build
pushd Build
cmake \
	-G Ninja \
	-DLLVM_ENABLE_PROJECTS="llvm;clang;clang-tools-extra" \
	-DBUILD_SHARED_LIBS=ON \
	-DLLVM_TARGETS_TO_BUILD=X86 \
	-DCMAKE_BUILD_TYPE=Release \
	-DLLVM_USE_LINKER=gold \
	-DLLVM_PARALLEL_LINK_JOBS=2 \
	../llvm

echo "Executing and logging build..."
TERM=dumb CodeChecker log -b "cmake --build . -- -j$(nproc)" -o "./compile_commands.json"
mv ./compile_commands.json ..
popd # llvm-project/Build
popd # llvm-project

echo "Done."

