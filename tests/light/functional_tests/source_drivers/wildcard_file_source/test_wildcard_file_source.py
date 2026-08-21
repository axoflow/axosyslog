#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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
import pytest

BASE_DIR = "wildcard"
MESSAGES_PER_TAG = 99
TAG_COUNT = 8
TEMPLATE = r'"${MSG}\n"'


def _messages(tag):
    return [
        "<7>2004-09-07T10:43:21+01:00 bzorp prog[12345]: {} {:05d}".format(tag, sequence_number)
        for sequence_number in range(1, MESSAGES_PER_TAG + 1)
    ]


def _tags():
    return ["wildcard{}".format(index) for index in range(TAG_COUNT)]


def _flat_path(index):
    return "{}.log".format(index % 4)


def _recursive_path(index):
    return "wildcard{}/{}.log".format(index % 2, index % 4)


def _write_tags(source, path_of):
    for index, tag in enumerate(_tags()):
        source.write_logs(path_of(index), _messages(tag))


def _assert_every_tag_is_delivered_in_order(file_destination, tags):
    lines = file_destination.read_logs(len(tags) * MESSAGES_PER_TAG)

    sequences = {}
    for line in lines:
        tag, sequence_number = line.rsplit(" ", 1)
        sequences.setdefault(tag, []).append(int(sequence_number))

    assert sorted(sequences.keys()) == sorted(tags)
    for tag in tags:
        assert sequences[tag] == list(range(1, MESSAGES_PER_TAG + 1))


def _create_config(config, monitor_method, **source_options):
    config.update_global_options(ts_format="iso", keep_hostname="yes", chain_hostnames="no")

    source = config.create_wildcard_file_source(
        base_dir=BASE_DIR,
        filename_pattern="*.log",
        monitor_method=monitor_method,
        recursive="yes",
        **source_options,
    )
    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE)
    config.create_logpath(statements=[source, file_destination])

    return source, file_destination


@pytest.mark.parametrize("monitor_method", ['"poll"', '"auto"'])
def test_wildcard_file_source(config, syslog_ng, monitor_method):
    source, file_destination = _create_config(config, monitor_method)
    source.base_dir.mkdir(parents=True, exist_ok=True)

    syslog_ng.start(config)
    _write_tags(source, _flat_path)

    _assert_every_tag_is_delivered_in_order(file_destination, _tags())


@pytest.mark.parametrize("monitor_method", ['"poll"', '"auto"'])
def test_wildcard_file_source_recursion(config, syslog_ng, monitor_method):
    source, file_destination = _create_config(config, monitor_method)
    source.base_dir.mkdir(parents=True, exist_ok=True)

    syslog_ng.start(config)
    _write_tags(source, _recursive_path)

    _assert_every_tag_is_delivered_in_order(file_destination, _tags())


@pytest.mark.parametrize("monitor_method", ['"poll"', '"auto"'])
def test_wildcard_file_source_base_dir_created_at_runtime(config, syslog_ng, monitor_method):
    source, file_destination = _create_config(config, monitor_method)

    syslog_ng.start(config)
    _write_tags(source, _flat_path)

    _assert_every_tag_is_delivered_in_order(file_destination, _tags())


def test_wildcard_file_source_exclude_pattern(config, syslog_ng):
    source, file_destination = _create_config(config, '"auto"', exclude_pattern='"*.?.log"')
    source.base_dir.mkdir(parents=True, exist_ok=True)

    syslog_ng.start(config)
    for index, tag in enumerate(_tags()):
        source.write_logs("excluded.{}.log".format(index % 4), _messages("excluded" + str(index)))
    _write_tags(source, _flat_path)

    _assert_every_tag_is_delivered_in_order(file_destination, _tags())
