#!/bin/env python3

"""
Count lines of code.
"""

import argparse
import dataclasses
import functools
import os
import pathlib
import sys
from typing import Generator, Iterable, Optional, TextIO


VERBOSE = False


@dataclasses.dataclass
class FileTypeDef:
    name: str
    filename_patterns: list[str]
    line_comment_beginning: str | None = None
    block_comment_pair: tuple[str, str] | None = None


@dataclasses.dataclass
class FileStats:
    path: pathlib.Path
    file_type: str
    file_size: int
    total_lines: int
    blank_lines: int
    comment_lines: int


@dataclasses.dataclass
class GroupedStats:
    file_type: str
    files: list[FileStats] = dataclasses.field(default_factory=list)
    file_size: int = 0
    total_lines: int = 0
    blank_lines: int = 0
    comment_lines: int = 0

    def __lshift__(self, s: FileStats):
        self.files.append(s)
        self.file_size += s.file_size
        self.total_lines += s.total_lines
        self.blank_lines += s.blank_lines
        self.comment_lines += s.comment_lines
        return self

    def __iadd__(self, s: 'GroupedStats'):
        self.files += s.files
        self.file_size += s.file_size
        self.total_lines += s.total_lines
        self.blank_lines += s.blank_lines
        self.comment_lines += s.comment_lines
        return self


FILE_TYPES: list[FileTypeDef] = [
    FileTypeDef('C/C++ header', ['*.h'], '//', ('/*', '*/')),
    FileTypeDef('C', ['*.c'], '//', ('/*', '*/')),
    FileTypeDef('C++', ['*.cc'], '//', ('/*', '*/')),
    FileTypeDef('CMake', ['CMakeLists.txt', '*.cmake'], '#'),
    FileTypeDef('Python', ['*.py'], '#'),
    FileTypeDef('Shell script', ['*.sh'], '#'),
    FileTypeDef('Def List', ['core/*.txt'], '#'),
    FileTypeDef('Conf/INI', ['*.conf', '*.ini'], '#'),
    FileTypeDef('ZiS', ['*.zis'], '#'),
]


def _measure_code_line_block_comment(stripped_line: str, file_type: FileTypeDef):
    """
    0 = none;
    1 = starts a block comment; 2 = starts a block comment, containing other code;
    3 = ends a block comment; 4 = ends a block comment, containing other code;
    5 = starts then ends block comments; 6 = starts then ends block comments, containing other code;
    7 = ends then starts block comments; 8 = ends then starts block comments, containing other code.
    """
    block_comment_pair = file_type.block_comment_pair
    if not block_comment_pair:
        return 0
    assert stripped_line == stripped_line.strip()
    beg_first = stripped_line.find(block_comment_pair[0])
    beg_last = stripped_line.rfind(block_comment_pair[0]) if beg_first != -1 else -1
    end_first = stripped_line.find(block_comment_pair[1])
    end_last = stripped_line.rfind(block_comment_pair[1]) if end_first != -1 else -1
    if beg_first != -1:
        if end_first != -1:
            if beg_first < end_last:
                return 5 if beg_first == 0 and end_last + len(block_comment_pair[1]) == len(stripped_line) else 6
            raise NotImplementedError()
        else:
            if beg_first != beg_last:
                raise RuntimeError(f'unmatched `{block_comment_pair[0]}`')
            return 1 if beg_first == 0 else 2
    else:
        if end_first != -1:
            if end_first != end_last:
                raise RuntimeError(f'unmatched `{block_comment_pair[1]}`')
            return 3 if end_last + len(block_comment_pair[1]) == len(stripped_line) else 4
        else:
            return 0


def scan_file(path: pathlib.Path) -> Optional[FileStats]:
    for file_type in FILE_TYPES:
        if any(map(path.match, file_type.filename_patterns)):
            break
    else:
        if VERBOSE:
            print('--', 'skip file', path)
        return
    total_lines, blank_lines, comment_lines = 0, 0, 0
    in_block_comment = False
    with open(path, 'r', newline='\n') as file:
        for line in file:
            line = line.strip()
            total_lines += 1
            if not in_block_comment:
                if not line:
                    blank_lines += 1
                elif file_type.line_comment_beginning and line.startswith(file_type.line_comment_beginning):
                    comment_lines += 1
                elif x := _measure_code_line_block_comment(line, file_type):
                    if x in (1, 2):
                        if x == 1:
                            comment_lines += 1
                        in_block_comment = True
                    elif x in (5, 6):
                        if x == 1:
                            comment_lines += 1
                    else:
                        raise RuntimeError('unexpected line: ' + line)
            else:
                comment_lines += 1
                if x := _measure_code_line_block_comment(line, file_type):
                    if x in (3, 4):
                        if x == 4:
                            comment_lines -= 1
                        in_block_comment = False
                    else:
                        raise RuntimeError('unexpected line: ' + line)
    return FileStats(
        path, file_type.name, path.stat().st_size,
        total_lines, blank_lines, comment_lines
    )


def scan_dir_or_file(path: pathlib.Path, excludes: Optional[list[str]] = None) -> Generator[FileStats, None, None]:
    if path.is_file():
        if x := scan_file(path):
            yield x
    elif path.is_dir():
        if path.name.startswith('.'): # NOTE: skip hidden folders
            return
        for sub_path in path.iterdir():
            if excludes and any(map(sub_path.match, excludes)):
                continue
            if sub_path.is_file():
                if x := scan_file(sub_path):
                    yield x
            elif sub_path.is_dir():
                for x in scan_dir_or_file(sub_path, excludes):
                    yield x


def dump_stats(stats_list: Iterable[GroupedStats], stream: TextIO, output_width: Optional[int] = None):
    stats_list = sorted(stats_list, key=lambda s: s.file_size, reverse=True)
    if not stats_list:
        return
    stats_total = GroupedStats('')
    for s in stats_list:
        stats_total += s

    line_width = output_width if output_width else os.get_terminal_size().columns if stream.isatty() else 80
    if line_width < 80:
        line_width = 80
    name_column_width = max(map(lambda t: len(t.name), FILE_TYPES)) + 2
    file_num_column_width = 6
    value_column_width = (line_width - name_column_width - file_num_column_width) // 5
    top_bot_rule = '=' * line_width
    mid_rule = '-' * (name_column_width - 1) + '+' + '-' * (line_width - name_column_width)
    sep_rule = '.' * (name_column_width - 1) + '|' + '.' * (line_width - name_column_width)

    puts = functools.partial(print, file=stream)
    putf = lambda name, files, percentage, total_ln, code_ln, blank_ln, comment_ln: \
        puts(
            f'{name:<{name_column_width - 1}}|'
            f'{files:>{file_num_column_width}}{percentage:>{value_column_width}}'
            f'{total_ln:>{value_column_width}}{code_ln:>{value_column_width}}'
            f'{blank_ln:>{value_column_width}}{comment_ln:>{value_column_width}}'
        )
    puts(top_bot_rule)
    putf('Language', 'Files', 'Proportion', 'Lines', 'Code Ln', 'Blank Ln', 'Comment Ln')
    puts(mid_rule)
    for s in stats_list:
        putf(
            s.file_type, len(s.files), format(s.file_size / stats_total.file_size * 100, '.1f') + '%',
            s.total_lines, s.total_lines - s.blank_lines - s.comment_lines, s.blank_lines, s.comment_lines,
        )
    puts(sep_rule)
    putf(
        '[SUM]', len(stats_total.files), '100%',
        stats_total.total_lines,
        stats_total.total_lines - stats_total.blank_lines - stats_total.comment_lines,
        stats_total.blank_lines, stats_total.comment_lines
    )
    puts(top_bot_rule)


def main():
    ap = argparse.ArgumentParser(description='Count lines of code.')
    ap.add_argument('-x', '--exclude', metavar='PATTERN', action='append')
    ap.add_argument('-w', '--width', type=int, required=False)
    ap.add_argument('-l', '--list', action='store_true')
    ap.add_argument('-v', '--verbose', action='store_true')
    ap.add_argument('FILE_OR_DIR', nargs='*')
    args = ap.parse_args()

    if args.verbose:
        global VERBOSE
        VERBOSE = True

    stats = {}
    for x in args.FILE_OR_DIR if args.FILE_OR_DIR else [os.getcwd()]:
        for s in scan_dir_or_file(pathlib.Path(x), args.exclude):
            try:
                gs = stats[s.file_type]
            except KeyError:
                gs = GroupedStats(s.file_type)
                stats[s.file_type] = gs
            if args.list:
                print('-', s)
            gs << s
    dump_stats(stats.values(), sys.stdout, args.width)


if __name__ == '__main__':
    main()
