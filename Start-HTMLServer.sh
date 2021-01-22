#!/bin/bash

if [ ! -d "./Results-Calculated" ]
then
	echo "No 'Results-Calculated' directory." >&2
	echo "Please run the analysis and then execute the calculator " \
		"script!" >&2
	exit 1
fi

pushd Results-Calculated

echo "Starting Web server on :8080 to serve reports..."
echo "Press C-c (^C) to terminate the server." >&2
python3 -m http.server 8080

echo "Web server quit." >&2

popd # Results-Calculated

