#!/usr/bin/env bash

set -e

usage() {
    cat <<EOF
Usage: finder-app/writer.sh <writefile> <writestr>
    <writefile>  The file to write to
    <writestr>   The string to write to the file

Example: finder-app/writer.sh /tmp/aesd/assignment1/sample.txt ios
EOF
}

if [[ "$1" == "-h" || "$1" == "--help" ]]
then
    usage
    exit 0
fi

if [[ $# -ne 2 ]]
then
    echo -e "Error: <writefile> and <writestr> must be specified.\n"
    usage
    exit 1
fi


WRITE_FILE=$1
WRITE_STR=$2

mkdir -p "$(dirname "${WRITE_FILE}")"
echo "${WRITE_STR}" > "${WRITE_FILE}"
if [[ $? -ne 0 ]]
then
    echo "Error: Could not create file ${WRITE_FILE}"
    exit 1
fi
