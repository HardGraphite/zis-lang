#!/bin/env python3

"""
cdocstr
=======

C doc-string collector. Collects ZiS doc-strings in C/C++ comments.

A doc-string comment should be like this:

/*#DOCSTR# <SYNOPSIS>
<DESCRIPTION>
...
<DESCRIPTION> */

SYNOPSIS should starts with "func" or "struct".
DESCRIPTION can contain Markdown-like markup sequences.
"""

import argparse
import dataclasses
import functools
from typing import Generator, Iterable, TextIO


class BadDocStr(RuntimeError):
    def __init__(self, file, line, what):
        super().__init__(file, line, what)

    def __str__(self) -> str:
        return f'{self.args[0]}:{self.args[1]}: {self.args[2]}'


@dataclasses.dataclass
class DocStr:
    title: str
    synopsis: str
    description: str

    @staticmethod
    def from_c_comment(comment_lines: list[str]) -> 'DocStr':
        assert comment_lines
        synopsis = comment_lines[0]
        title = synopsis.split('(', 1)[0].capitalize()
        desc_list = []
        for x in comment_lines[1:]:
            if x:
                desc_list.append(x)
                desc_list.append(' ')
            else:
                if desc_list:
                    desc_list[-1] = '\n'
        if desc_list and desc_list[-1] in (' ', '\n'):
            desc_list.pop()
        return DocStr(title, synopsis, ''.join(desc_list))


def collect_docstr(file: TextIO) -> Generator[DocStr, None, None]:
    current_comment = []
    line_number = 0
    for line in file:
        line_number += 1
        line = line.strip()
        if not current_comment:
            if line.startswith('/*#DOCSTR# '):
                current_comment.append(line[11:])
        else:
            if line.endswith('*/'):
                current_comment.append(line[:-2].rstrip())
                yield DocStr.from_c_comment(current_comment)
                current_comment.clear()
            else:
                current_comment.append(line)
    if current_comment:
        raise BadDocStr(file.name, line_number, 'comment not terminated')


def generate_markdown(title: str, entries: Iterable[DocStr], file: TextIO):
    puts = functools.partial(print, file=file)
    puts('#', title.replace('\\', '\\\\'))
    puts()
    for x in entries:
        puts('###', x.title.replace('\\', '\\\\'))
        puts()
        puts('```')
        puts(x.synopsis)
        puts('```')
        puts()
        puts(x.description)
        puts()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-t', '--title')
    ap.add_argument('-o', '--output', metavar='OUT_FILE', required=False)
    ap.add_argument('INPUT', nargs='+')
    args = ap.parse_args()

    docstrs = []
    for in_file in args.INPUT:
        with open(in_file) as f:
            docstrs += list(collect_docstr(f))
    if args.output is not None:
        with open(args.output, 'w', newline='\n') as f:
            generate_markdown(args.title, docstrs, f)
    else:
        import sys
        generate_markdown(args.title, docstrs, sys.stdout)


if __name__ == '__main__':
    main()
