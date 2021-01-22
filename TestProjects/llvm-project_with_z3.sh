#!/bin/bash


# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	unzip

echo "Cloning project..."
git clone http://github.com/llvm/llvm-project.git llvm-project_with_z3

pushd llvm-project_with_z3
git checkout llvmorg-9.0.0

echo "Downloading dependencies..."
wget http://github.com/Z3Prover/z3/releases/download/z3-4.8.9/z3-4.8.9-x64-ubuntu-16.04.zip -O z3.zip
unzip z3.zip
rm z3.zip
mv z3-* z3-install

export LD_LIBRARY_PATH="$(pwd)/z3-install/bin:$LD_LIBRARY_PATH"

echo "Generating build configuration..."
mkdir Build
pushd Build
cmake \
	-G Ninja \
	-DLLVM_ENABLE_PROJECTS="llvm;clang" \
	-DLLVM_Z3_INSTALL_DIR="$(dirname $(pwd))/z3-install" \
	-DLLVM_ENABLE_Z3_SOLVER=ON \
	-DBUILD_SHARED_LIBS=ON \
	-DLLVM_TARGETS_TO_BUILD=X86 \
	-DCMAKE_BUILD_TYPE=Release \
	-DLLVM_USE_LINKER=gold \
	-DLLVM_PARALLEL_LINK_JOBS=2 \
	../llvm

echo "Executing and logging build..."
TERM=dumb CodeChecker log -b "cmake --build . -- -j$(nproc)" -o "./compile_commands.json"
mv ./compile_commands.json ..
popd # llvm-project_with_z3/Build
popd # llvm-project_with_z3

echo "Done."

