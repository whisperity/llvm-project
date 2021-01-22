#!/bin/bash

if [ -z "$(command -v CodeChecker)" ]
then
	echo "CodeChecker binary not found in PATH!" >&2
	exit 1
fi

CodeChecker server --stop-all
