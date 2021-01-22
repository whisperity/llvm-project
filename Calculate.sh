#!/bin/bash

echo "Check if CodeChecker server is running..."
CodeChecker server --list | grep 8001
if [ $? -ne 0 ]
then
	echo "No CodeChecker server running! Please start, and ensure the analyses are done." >&2
	exit 1
fi

PRODUCT_URL="http://localhost:8001/AdjacentParams"

echo
echo "This script might take a long time, depending on I/O speed of the machine!"
echo
echo
echo

echo "Generating Markdown reports..."
pushd ./ReportGen

mkdir -p Reports
pushd Reports

exec 5>&1
for PROJECT in $(python3 \
  ../__main__.py \
  --url "${PRODUCT_URL}" \
  --list \
  | tee /dev/fd/5)
do
	date
	echo "Generating report for project ${PROJECT}..." >&2
	python3 ../__main__.py \
		--url "${PRODUCT_URL}" \
		--name "${PROJECT}" \
		> "${PROJECT}.md"
done

echo "Converting Markdown to HTML..."

for PROJECT in $(python3 \
  ../__main__.py \
  --url "${PRODUCT_URL}" \
  --list \
  | tee /dev/fd/5)
do
	date
	echo "Converting report for project ${PROJECT} to HTML..." >&2
	pandoc "${PROJECT}.md" \
		--self-contained \
		--metadata pagetitle="Results for ${PROJECT}" \
		--toc \
		> "${PROJECT}.html"
done

popd # ReportGen/Reports

echo "Generating result structure..." >&2
mv Reports ../Results-Calculated

popd # ReportGen

pushd Results-Calculated

mkdir md html
mv ./*.md ./md/
mv ./*.html ./html/

popd # Results-Calculated

echo "Done."

