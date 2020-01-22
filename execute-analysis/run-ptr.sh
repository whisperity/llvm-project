#!/bin/bash

JOBS=$(nproc)

PRODUCT_URL="http://localhost:8001/Default"
if [ $# -eq 1 -a ! -z "$1" ]
then
  PRODUCT_URL="$1"
  echo "Using CodeChecker product URL ${PRODUCT_URL}." >&2
fi

if [ -z $(which CodeChecker) ]
then
	echo "No CodeChecker!" >&2
	exit 1
fi


MAIN_DIR=$(pwd)

pushd ./TestProjects

for BJSON in $(fd compile_commands.json -d 2 -H -I ./)
do
	echo -n "Running for "
	PROJECT=$(dirname ${BJSON})
	echo "${PROJECT}..."
	echo -e "\tJSON at: ${BJSON}"

  RUN_NAME="${PROJECT}"
  echo -e "\tAs project name: ${RUN_NAME}"

  CodeChecker analyze \
    --analyzers clang-tidy \
    --enable readability-redundant-pointer- \
    --disable Weverything \
    --jobs ${JOBS} \
    --output "../Reports-Ptr/${RUN_NAME}" \
    ${BJSON}

  if [ $? -ne 0 ]
  then
    echo "Analysis failed!" >&2
    continue
  fi

  CodeChecker store \
    --url "${PRODUCT_URL}" \
    --name "${RUN_NAME}" \
    "../Reports-Ptr/${RUN_NAME}/" \
    --trim-path-prefix "$(dirname $(realpath -s ${BJSON}))" \

  if [ $? -ne 0 ]
  then
    echo "Store failed!" >&2
    continue
  fi
done

popd
