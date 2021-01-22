#!/bin/bash

netstat -tl | grep ":8081"
if [ $? -eq 0 ]
then
	echo "Found CodeChecker server to be running. Please stop server first!" >&2
	exit 1
fi

netstat -tl | grep ":http-alt"
if [ $? -eq 0 ]
then
	echo "Found HTML result server to be running. Please stop server first!" >&2
	exit 1
fi

if [ "$1" != "--yes" ]
then
	echo
	echo "-----------------------------------------------------------------"
	echo "!!! WARNING !!!"
	echo
	echo "This script will WIPE all intermediate and final results from this"
	echo "computer, including downloaded projects, build output, analysis"
	echo "results, server contents and files."
	echo
	echo
	echo "  THIS ACTION HAS THE POSSIBILITY OF DESTROYING DAYS' WORTH OF "
	echo "      COMPUTATIONAL EFFORT!"
	echo
	echo
	echo "Please run the script by specifying '--yes' as its argument"
	echo "if you know what you're doing!"
	echo
	echo "-----------------------------------------------------------------"

	exit 1
fi

echo
echo "BEGIN CLEANING. IT WILL ALL BE GONE..."
echo "Removing rendered output..."
rm -r ./Results-Calculated

echo "Removing raw analysis output..."
rm -r ./Reports-Matches

echo "Removing test projects..."
pushd ./TestProjects
for PROJECT in $(fd --type directory --exact-depth 1)
do
	echo "Clearing ${PROJECT}..."
	rm -rf "./${PROJECT}"
done
popd # TestProjects

echo "Destroying CodeChecker database..."
rm -rf ./.codechecker ./.codechecker.*.json

echo "Done!"
