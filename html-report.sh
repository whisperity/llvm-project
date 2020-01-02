#!/bin/bash

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

for PROJECT in $(python3 ../__main__.py --url "${PRODUCT_URL}" --list)
do
  python3 ../__main__.py \
    --url "${PRODUCT_URL}" \
    --name "${PROJECT}" \
    > "${PROJECT}.md"

  pandoc "${PROJECT}.md" \
    --self-contained \
    --metadata pagetitle="Results for ${PROJECT}" \
    --toc \
    > "${PROJECT}.html"
done

popd # Reports
