# This script can be used to add up the results of a Calculate.sh **MARKDOWN**
# file into a sum for a configuration in the project.
#  - Integral and floating types are put into the same numeric bucket
#  - Pointer to X and X is put into the same bucket

import pprint
import sys


def printDict(blockDict):
    def get_and_del(key):
        try:
            x = blockDict[key]
            del blockDict[key]
            return x
        except KeyError:
            return 0

    print("Fundamental numeric:", get_and_del("fundamental numeric"))
    print("Custom numeric:", get_and_del("custom numeric"))
    print("Array", get_and_del("buffer (C-style array)"))
    print("Buffer", get_and_del("buffer (void-ptr or templated)"))
    print("String", get_and_del("strings of buffer (char-ptr)") +
          get_and_del("string-like"))
    print("Framework", get_and_del("framework type"))
    print("Custom", get_and_del("unknown?"))

    if blockDict:
        print("Others:")
        pprint.pprint(blockDict)

configurationFound = False
readingTypeBlockStartedAt = 0
blockParsed = dict()

with open(sys.argv[1], 'r') as infile:
    for idx, line in enumerate(infile):
        lineMatch = line.lstrip("# ").rstrip()
        if lineMatch.startswith("Project: "):
            print(line)
        elif lineMatch.startswith("Configuration: "):
            if lineMatch.replace("Configuration: ", "") == sys.argv[2]:
                configurationFound = True
                print(line)
            else:
                configurationFound = False

        if not configurationFound:
            continue

        if lineMatch == "Distribution of types involved in mixup:":
            readingTypeBlockStartedAt = idx
        elif readingTypeBlockStartedAt and \
                idx > readingTypeBlockStartedAt + 2 and \
                not lineMatch:
            readingTypeBlockStartedAt = 0

            printDict(blockParsed)
            blockParsed.clear()
            print()
        elif readingTypeBlockStartedAt:
            parts = lineMatch.split("*")
            if len(parts) != 4:
                continue
            kind, count = parts[1].strip().rstrip(":"), int(parts[2])

            if kind.startswith("pointer to "):
                kind = kind.replace("pointer to ", "")
            if kind.endswith((" integral", " floating")):
                kind = kind.replace("integral", "numeric") \
                    .replace("floating", "numeric")

            blockParsed[kind] = blockParsed.get(kind, 0) + count
