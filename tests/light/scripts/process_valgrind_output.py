#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Andras Mitzki <andras.mitzki@axoflow.com>
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
import os
import re
from typing import Iterable
from typing import List

# precompiled patterns
definitely_pat = re.compile(r'definitely lost in', re.IGNORECASE)
generic_pat = re.compile(r'bytes in ')
errors_in_context_pat = re.compile(r'errors in context', re.IGNORECASE)
generic_errors_in_context_pat = re.compile(r'== $', re.IGNORECASE)
_pid_re = re.compile(r'==\d+==\s*')
_exclude_re = re.compile(r'blocks are definitely lost in loss record')


def find_all_valgrind_logs(root_dir: str) -> List[str]:
    """Return list of files under root_dir whose names end with '_valgrind_output'."""
    valgrind_logs = []
    for dirpath, _, filenames in os.walk(root_dir):
        for filename in filenames:
            if filename.endswith("_valgrind_output"):
                valgrind_logs.append(os.path.join(dirpath, filename))
    return valgrind_logs


def file_contains_definitely_lost(file_path: str) -> bool:
    """Quick scan to decide whether a file has 'definitely lost' (avoids loading large files unnecessarily)."""
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            if 'definitely lost' in line:
                return True
    return False


def file_contains_errors_in_context(file_path: str) -> bool:
    """Quick scan to decide whether a file has 'errors in context'."""
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            if 'errors in context' in line:
                return True
    return False


def extract_definitely_lost_blocks_from_lines(lines: Iterable[str]) -> List[List[str]]:
    """
    Parse lines and return a list of 'definitely lost' blocks.
    """
    blocks: List[List[str]] = []
    current: List[str] | None = None

    for line in lines:
        if definitely_pat.search(line):
            if current:
                blocks.append(current)
            current = [line.strip()]
            continue

        if generic_pat.search(line):
            if current:
                blocks.append(current)
                current = None
            continue

        if current is not None:
            current.append(line.strip())

    if current:
        blocks.append(current)

    return blocks


def extract_errors_in_context_blocks_from_lines(lines: Iterable[str]) -> List[List[str]]:
    """
    Parse lines and return a list of 'errors in context' blocks.
    """
    blocks: List[List[str]] = []
    current: List[str] | None = None

    for line in lines:
        if errors_in_context_pat.search(line):
            if current:
                blocks.append(current)
            current = [line.strip()]
            continue

        if generic_errors_in_context_pat.search(line):
            if current:
                blocks.append(current)
                current = None
            continue

        if current is not None:
            current.append(line.strip())

    if current:
        blocks.append(current)

    return blocks


def extract_definitely_lost_blocks_from_file(file_path: str) -> List[List[str]]:
    """Open file and extract blocks if it contains 'definitely lost'."""
    if not file_contains_definitely_lost(file_path):
        return []
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        return extract_definitely_lost_blocks_from_lines(f)


def extract_errors_in_context_blocks_from_file(file_path: str) -> List[List[str]]:
    """Extract blocks containing 'errors in context' from a file."""
    if not file_contains_errors_in_context(file_path):
        return []
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        return extract_errors_in_context_blocks_from_lines(f)


def _normalize_line(line: str) -> str:
    """Remove Valgrind PID markers like '==606373== ' and trim."""
    return _pid_re.sub('', line).strip()


def dedupe_blocks(blocks: List[List[str]]) -> List[List[str]]:
    """
    Deduplicate blocks based on their content, ignoring metadata lines.
    Returns a list of unique blocks.
    """

    content_map = {}
    for block in blocks:
        meta = _normalize_line(block[0])
        block_content = tuple(_normalize_line(line) for line in block[1:] if not line.endswith("=="))
        if block_content not in content_map:
            content_map.update({block_content: []})
        content_map[block_content].append(meta)

    return content_map


def write_file(file_path: str, blocks: List[List[str]]) -> None:
    """Write blocks to a file."""
    with open(file_path, 'w', encoding='utf-8') as f:
        block_counter = 0
        for block_content, metas in blocks.items():
            block_counter += 1
            all_bytes = []
            for meta in metas:
                all_bytes.append(int(meta.split(' ')[0].strip().replace(',', '')))

            f.write(f"\nBlock {block_counter}, number of occurrences in all tests: {len(metas)}, leaked bytes:\n {list(reversed(sorted(all_bytes)))}\n")
            for line in block_content:
                f.write(f"{line}\n")


def main(root_dir: str) -> None:
    files = find_all_valgrind_logs(root_dir)
    assert files, f"No valgrind log files found in {root_dir}"
    all_definitely_lost_blocks: List[List[str]] = []
    all_errors_in_context_blocks: List[List[str]] = []
    for file in files:
        all_definitely_lost_blocks.extend(extract_definitely_lost_blocks_from_file(file))
        all_errors_in_context_blocks.extend(extract_errors_in_context_blocks_from_file(file))
    unique_definitely_lost_blocks = dedupe_blocks(all_definitely_lost_blocks)
    write_file("definitely_lost_blocks.txt", unique_definitely_lost_blocks)

    unique_errors_in_context_blocks = dedupe_blocks(all_errors_in_context_blocks)
    write_file("errors_in_context_blocks.txt", unique_errors_in_context_blocks)


if __name__ == "__main__":
    main("./reports/")
