#!/bin/bash

## Converts outputs from `backtrace_symbols_fd()` to detailed information.
## Backtrace lines are read from stdin.
## Accepts an optional commadline argument that indicates the cwd.

if [[ $# -gt 0 ]]; then
    cd "$1"
fi

index=0
while read bt_line; do
    if [[ $bt_line =~ ^(.+)\((.+)\)\[.+\]$ ]]; then
        exe_path="${BASH_REMATCH[1]}"
        addr_info="${BASH_REMATCH[2]}"
        line_info=$(addr2line -pCfe "$exe_path" "$addr_info" 2>/dev/null)
        if [[ $? != 0 ]] || [ "$line_info" = '?? ??:0' ]; then
            line_info="?? at ??:?"
        fi
        echo "$index:" "$line_info" "[$exe_path!$addr_info]"
        index=$(($index + 1))
    fi
done
