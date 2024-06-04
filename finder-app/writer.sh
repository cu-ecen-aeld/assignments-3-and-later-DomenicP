#!/usr/bin/env bash

# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

readonly SUCCESS=0
readonly FAIL=1

main() {
    check_argc "$@"
    local writefile="$1"
    local writestr="$2"

    # Create the parent directory if it doesn't already exist
    parent_dir=$(dirname "$writefile")
    mkdir -p "$parent_dir"

    # Write the string to the file
    echo "$writestr" > "$writefile" || echo "Could not write to file ${writefile}"

    exit $SUCCESS
}

check_argc() {
    if [ "$#" -ne 2 ]; then
        echo "Usage: writer.sh WRITEFILE WRITESTR"
        exit $FAIL
    fi
}

main "$@"
