#!/usr/bin/env python3
"""Emits the --names JSON tut2catch.py takes, from the comments above each test.

TUT has no test names, so a TUT test's name is whatever prose the comment above
it carries. tut2catch.py reads that comment where it recognises it as prose and
falls back to "#N" where it does not -- a name with "->" in it, say, or one the
writer never gave a comment. This reads the same comments and prints them as the
override mapping, so every case leaves the converter with the name its author
wrote, minus the "Test N: " prefix the numbering no longer needs.

    python3 scripts/testing/tut_names.py indra/llimage/tests/*.cpp > names.json
    python3 scripts/testing/tut2catch.py --names names.json --dry-run <files>

Groups are read from the testgroup("Name") object, and a test's name from the
"// Test N: <text>" comment above it, which may be several lines up and split
across lines. A test with no such comment keeps the converter's own choice: it
is left out of the mapping rather than named "#N" here.
"""

import json
import re
import sys

GROUP_RE = re.compile(r'^\s*(?:tut::)?\w+\s+\w+\s*\(\s*"([^"]+)"\s*\)\s*;')
TEST_RE = re.compile(r'::\s*test<(\d+)>\s*\(\s*\)')


def group_of(lines):
    """The group's name, from the testgroup("Name") object that declares it."""
    for line in lines:
        found = GROUP_RE.search(line)
        if found:
            return found.group(1)
    return None


def name_above(lines, index):
    """The prose comment above a test<N>, joined, or None where there is none.

    Walks up over the two `template<>` lines a specialisation takes and any
    blank lines, then reads comment lines upward until it leaves the comment
    block or runs out. Separator rules ("// ----") end it rather than naming
    anything, and code ends it too, so a test with no comment of its own does
    not pick up the last line of the test before it. A name that spans lines
    continues, which is why the result is joined rather than taken from one.
    """
    parts = []
    started = False
    for line in reversed(lines[:index]):
        text = line.strip()
        if not started:
            if not text or re.fullmatch(r'template\s*<>(\s*template\s*<>)?', text):
                continue
            if not text.startswith('//'):
                return None
            started = True
        if not text.startswith('//'):
            break
        body = text[2:].strip()
        if not body or re.fullmatch(r'[-=*~_#\s]*', body):
            # A separator rule: a heading's edges, not part of a name.
            break
        parts.append(body)
    if not parts:
        return None
    name = " ".join(reversed(parts))
    name = re.sub(r'\s*->\s*', ' to ', name)
    name = re.sub(r'^\s*Test\s+\d+\s*[:.]\s*', '', name)
    name = re.sub(r'\s+', ' ', name).strip()
    return name or None


def main(argv):
    if not argv:
        sys.exit("usage: tut_names.py <tut file>...")
    mapping = {}
    for path in argv:
        with open(path, encoding="utf-8") as handle:
            lines = handle.read().splitlines()
        group = group_of(lines)
        if not group:
            # Already Catch2, or not a TUT file at all: nothing here to name.
            print(f"{path}: no TUT group, skipped", file=sys.stderr)
            continue
        names = {}
        for index, line in enumerate(lines):
            found = TEST_RE.search(line)
            if not found:
                continue
            name = name_above(lines, index)
            if name:
                names[found.group(1)] = name
        if names:
            mapping[path] = {group: names}
    json.dump(mapping, sys.stdout, indent=1, sort_keys=True)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main(sys.argv[1:])