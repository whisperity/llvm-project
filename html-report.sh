#!/bin/bash

PRODUCT_URL="http://localhost:8001/Default"
if [ $# -ge 1 -a ! -z "$1" ]
then
  PRODUCT_URL="$1"
  echo "Using CodeChecker product URL ${PRODUCT_URL}." >&2
fi

FUNCTIONS_URL="http://localhost:8001/Functions"
if [ $# -ge 2 -a ! -z "$2" ]
then
  FUNCTIONS_URL="$2"
  echo "Using CodeChecker product URL for function counts ${FUNCTIONS_URL}." >&2
fi

if [ -z $(which CodeChecker) ]
then
	echo "No CodeChecker!" >&2
	exit 1
fi
if [ -z $(which pandoc) ]
then
	echo "No 'pandoc' found!" >&2
	exit 1
fi
if [ -z $(find __main__.py 2>/dev/null) ]
then
  echo "Error: run script from the location where the Python code is!" >&2
  exit 1
fi

mkdir -p Reports
pushd Reports

exec 5>&1
for PROJECT in $(python3 \
  ../__main__.py \
  --url "${PRODUCT_URL}" \
  --list \
  | tee /dev/fd/5)
do
  python3 ../__main__.py \
    --url "${PRODUCT_URL}" \
    --functions-url "${FUNCTIONS_URL}" \
    --name "${PROJECT}" \
    | tee "${PROJECT}.md"

  pandoc "${PROJECT}.md" \
    --self-contained \
    --metadata pagetitle="Results for ${PROJECT}" \
    --toc \
    > "${PROJECT}.html"
done

popd # Reports
