#!/bin/bash


# Install some necessary requirements to make the project build.
sudo apt update
sudo apt install --no-install-recommends --yes \
	libboost-filesystem-dev \
	libboost-log-dev \
	libboost-program-options-dev \
	libboost-regex-dev \
	\
	llvm-10 llvm-10-dev \
	clang-10 libclang-10-dev \
	\
	default-jdk \
	\
	libsqlite3-dev \
	\
	ctags \
	libgit2-dev \
	libmagic-dev \
	\
	libgraphviz-dev \
	npm \
	\
	libgtest-dev \
	\
	libssl1.0-dev

echo "Installing self-hosted third-party dependencies..."

sudo apt install --no-install-recommends --yes \
	byacc \
	curl \
	flex \
	gcc-7-plugin-dev \
	wget

mkdir CodeCompass
pushd CodeCompass

echo "    -> Build2 system..."
wget https://download.build2.org/0.13.0/build2-install-0.13.0.sh
sh build2-install-0.13.0.sh --yes --trust yes "$(pwd)/b2-install"
export PATH="$(pwd)/b2-install/bin:$PATH"

echo "    -> ODB (configure)..."
mkdir odb-build
pushd odb-build

bpkg create --quiet --jobs $(nproc) cc \
	config.cxx=g++ config.cc.coptions=-O3 \
	config.bin.rpath="$(dirname $(pwd))/odb-install/lib" \
	config.install.root="$(dirname $(pwd))/odb-install" \
	config.install.sudo=sudo
bpkg add https://pkg.cppget.org/1/beta --trust-yes
bpkg fetch --trust-yes

echo "    -> ODB (building)..."
bpkg build odb --yes
bpkg build libodb --yes
bpkg build libodb-sqlite --yes

echo "    -> ODB (install)..."
bpkg install --all --recursive

popd # CodeCompass/odb-build

echo "    -> GoogleTest..."
mkdir googletest
pushd googletest

cp -R /usr/src/googletest/* .
mkdir build
pushd build

cmake .. -DCMAKE_INSTALL_PREFIX="$(dirname $(dirname $(pwd)))/gtest-install"
make install -j $(nproc)

popd # CodeCompass/googletest/build
popd # CodeCompass/googletest

echo "    -> Thrift..."
mkdir thrift
pushd thrift
wget -O thrift-0.13.0.tar.gz "http://www.apache.org/dyn/mirrors/mirrors.cgi?action=download&filename=thrift/0.13.0/thrift-0.13.0.tar.gz"
tar -xvf ./thrift-0.13.0.tar.gz
mv thrift-0.13.0 src
pushd src

echo "    -> Thrift (configure)..."
./configure --prefix="$(dirname $(pwd))/install" --without-python         \
	--enable-libtool-lock --enable-tutorial=no --enable-tests=no     \
	--with-libevent --with-zlib --without-nodejs --without-lua       \
	--without-ruby --without-csharp --without-erlang --without-perl  \
	--without-php --without-php_extension --without-dart             \
	--without-haskell --without-go --without-rs --without-haxe       \
	--without-dotnetcore --without-d --without-qt4 --without-qt5     \
	--without-java

echo "    -> Thrift (build)..."
make install -j $(nproc)

popd # CodeCompass/thrift/src
popd # CodeCompass/thrift

export CMAKE_PREFIX_PATH="$(pwd)/odb-install:$(pwd)/thrift/install:$CMAKE_PREFIX_PATH"
export PATH="$(pwd)/odb-install/bin:$(pwd)/thrift/install/bin:$PATH"
export GTEST_ROOT="$(pwd)/gtest-install"

echo "Cloning project..."
git clone http://github.com/Ericsson/CodeCompass.git

pushd CodeCompass
git checkout 1796dcb1a2538c541d372e5e673d11438cd9b7b2

echo "Generating build configuration..."
mkdir Build
cd Build
cmake \
	-DDATABASE="sqlite" \
	-DCMAKE_BUILD_TYPE="Release" \
	-DCMAKE_INSTALL_PREFIX="$(dirname $(pwd))/Install" \
	-DLLVM_DIR="/usr/lib/llvm-10/cmake" \
	-DClang_DIR="/usr/lib/cmake/clang-10" \
	..

echo "Executing and logging build..."
TERM=dumb CodeChecker log -b "cmake --build . -- -j$(nproc)" -o "./compile_commands.json"
mv ./compile_commands.json ../..
popd # CodeCompass/CodeCompass

popd # CodeCompass

echo "Done."

