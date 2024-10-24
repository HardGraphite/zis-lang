#!/bin/bash

## Analyze coverage test results.

if [[ $# -eq 0 ]] || [[ $# -gt 2 ]] || [[ ! -d "$1" ]]; then
    echo "Usage: $0 <BUILD_DIR> [<TOOL_NAME>]"
    echo '  where TOOL_NAME is any of gcov,lcov,gcovr,rm'
    exit
fi

BUILD_DIR="$1"
TOOL_NAME="$2"

SOURCE_DIR="$(realpath "$(dirname "$0")/..")"
COVERAGE_DIR="$BUILD_DIR/coverage"
REPORT_DIR="$COVERAGE_DIR/report"

function ensure_dir {
    for d in "$@"; do
        local d=${!d}
        [[ -d "$d" ]] || mkdir "$d"
    done
}

function use_gcov {
    ensure_dir COVERAGE_DIR
    cd "$COVERAGE_DIR"
    find "$BUILD_DIR" -type f -name '*.gcda' | xargs gcov --demangled-names
}

function use_lcov {
    ensure_dir COVERAGE_DIR REPORT_DIR
    cd "$BUILD_DIR"
    local tracefile="$COVERAGE_DIR/coverage.info"
    lcov --capture --directory "$BUILD_DIR" --output-file "$tracefile" \
        --rc branch_coverage=1 --demangle-cpp --no-external --omit-lines 'assert.+'
    genhtml "$tracefile" --output-directory "$REPORT_DIR"
}

function use_gcovr {
    ensure_dir COVERAGE_DIR REPORT_DIR
    cd "$BUILD_DIR"
    gcovr "$BUILD_DIR" --root "$SOURCE_DIR" --exclude '.*/test/.+' --html-nested "$REPORT_DIR/coverage.html" \
        --gcov-object-directory "$COVERAGE_DIR" --exclude-lines-by-pattern '^\s*assert.+'
}

function delete_gcov_files {
    find "$BUILD_DIR" -name '*.gcda' | xargs rm
}

case "$TOOL_NAME" in
    gcov)
        use_gcov
        ;;
    lcov)
        use_lcov
        ;;
    gcovr)
        use_gcovr
        ;;
    '')
        if which gcovr; then
            use_gcovr
        elif which lcov; then
            use_lcov
        elif which gcov; then
            use_gcov
        else
            exit 1
        fi
        ;;
    rm)
        delete_gcov_files
        ;;
    *)
        echo 'Unknown tool' $TOOL_NAME
        exit 1
        ;;
esac
