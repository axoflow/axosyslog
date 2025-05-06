#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
# Copyright (c) 2025 László Várady
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################


def test_filterx_dict_unset_key_with_hash_collision(syslog_ng, config):
    source = config.create_example_msg_generator_source(num=1, template='"id=a m=b"')
    filterx = config.create_filterx(r"""
        log = {"id": "a", "m": "b"};
        unset(log.id);
        $MSG = log.m;
""")
    destination = config.create_file_destination(file_name="output.log", template='"$MSG\n"')

    config.create_logpath(statements=[source, filterx, destination])

    syslog_ng.start(config)
    assert destination.read_log() == "b"


def test_filterx_dict_message_value_key(syslog_ng, config):
    source = config.create_example_msg_generator_source(num=1, template='test_key')
    filterx = config.create_filterx(r"""
        d = {};
        d["test_key"] = "test_value";
        $MSG = d[$MSG];
""")
    destination = config.create_file_destination(file_name="output.log", template='"$MSG\n"')

    config.create_logpath(statements=[source, filterx, destination])

    syslog_ng.start(config)
    assert destination.read_log() == "test_value"
