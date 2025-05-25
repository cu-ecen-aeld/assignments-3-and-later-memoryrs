#!/bin/sh

set -e

usage() {
    cat <<EOF
Usage: finder-app/finder.sh <filesdir> <searchstr>
    <filesdir>   A path to a directory on the filesystem
    <searchstr>  A text string which will be searched within these files under filesdir

Example: finder-app/finder.sh /tmp/aesd/assignment1 linux
EOF
}

if [[ "$1" == "-h" || "$1" == "--help" ]]
then
    usage
    exit 0
fi

if [[ $# -ne 2 ]]
then
    echo -e "Error: <filesdir> and <searchstr> must be specified.\n"
    usage
    exit 1
fi


FILES_DIR=$1
SEARCH_STR=$2

if [[ -d "${FILES_DIR}" ]]
then
    file_count=$(find ${FILES_DIR} -type f | wc -l)
    matching_lines=$(grep ${SEARCH_STR} -r ${FILES_DIR} | wc -l)
    echo "The number of files are $file_count and the number of matching lines are $matching_lines"
else
    echo -e "Error: ${FILES_DIR} is not a directory or cannot access '${FILES_DIR}': No such directory.\n"
    usage
    exit 1
fi
