#!/usr/bin/env bash

# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

readonly SUCCESS=0
readonly FAIL=1

main() {
    check_argc "$@"
    local filesdir="$1"
    local searchstr="$2"
    check_filesdir "$filesdir"
    search_dir_for_str "$filesdir" "$searchstr"
    exit $SUCCESS
}

check_argc() {
    # Check for exactly two arguments
    if [ "$#" -ne 2 ]; then
        # Print usage information and exit
        echo "Usage: finder.sh FILESDIR SEARCHSTR"
        exit $FAIL
    fi
}

check_filesdir() {
    # Check if the argument is a directory
    if [ ! -d "$1" ]; then
        # Print error message and exit
        echo "$1 is not a directory"
        exit $FAIL
    fi
}

search_dir_for_str() {
    local filesdir="$1"
    local searchstr="$2"

    # Recursively search for the string across files with grep
    lines=$(grep -R "$searchstr" "$filesdir")

    # Count the number of unique files
    num_files=$(echo "$lines" | awk -F: '{ print $1 }' | uniq | wc -l)

    # Count the number of lines
    num_lines=$(echo "$lines" | wc -l)

    # Print the results
    echo "The number of files are ${num_files} and the number of matching lines are ${num_lines}"
}

main "$@"
