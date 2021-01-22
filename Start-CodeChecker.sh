#!/bin/bash

if [ -z "$(command -v CodeChecker)" ]
then
	echo "CodeChecker binary not found in PATH!" >&2
	exit 1
fi

echo
echo "DO NOT EXIT THIS SCRIPT UNTIL IT REACHES ITS COMPLETION!"
echo
echo

echo "Check if CodeChecker server is running..."

echo "Check if databases are properly configured..."
CodeChecker cmd products list | grep "AdjacentParams"
if [ $? -ne 0 ]
then
	echo "No databases found for the results! Configuring..." >&2

	CodeChecker cmd products add \
		--name "Adjacent Parameters' warnings" \
		--description "The reports of the analysis rule on the projects as the users will see them." \
		AdjacentParams
fi

echo "Server should be running and configured now..."

