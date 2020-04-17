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
CONFIGURATIONS=$(fd 'aa-.*.txt' -d 1 ./)

pushd ./TestProjects

for BJSON in $(fd compile_commands.json -d 2 -H -I ./)
do
	echo -n "Running for "
	PROJECT=$(dirname ${BJSON})
	echo "${PROJECT}..."
	echo -e "\tJSON at: ${BJSON}"

	for CFG in ${CONFIGURATIONS}
	do
		echo -e "\n\tUsing configuration: ${CFG}"
		cat ${MAIN_DIR}/${CFG}

		RUN_NAME="${PROJECT}__$(echo ${CFG} | sed "s/aa-//" | sed "s/\.txt//")"
		echo -e "\tAs project name: ${RUN_NAME}"

		CodeChecker analyze \
			--analyzers clang-tidy \
			--enable experimental-cppcoreguidelines-avoid-adjacent-parameters-of-the-same-type \
			--disable Weverything \
			--jobs ${JOBS} \
			--tidyargs $(realpath --relative-to=$(pwd) "${MAIN_DIR}/${CFG}") \
			--output "../Reports-AA/${RUN_NAME}" \
			${BJSON}

		if [ $? -ne 0 ]
		then
			echo "Analysis failed!" >&2
			continue
		fi

		CodeChecker store \
		  --url "${PRODUCT_URL}" \
			--name "${RUN_NAME}" \
			"../Reports-AA/${RUN_NAME}/" \
			--trim-path-prefix "$(dirname $(realpath -s ${BJSON}))" \

		if [ $? -ne 0 ]
		then
			echo "Store failed!" >&2
			continue
		fi
	done
done

popd
