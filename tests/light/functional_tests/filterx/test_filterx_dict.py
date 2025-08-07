#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
# Copyright (c) 2025 László Várady
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


def test_filterx_dict_nullv_elements(syslog_ng, config):
    source = config.create_example_msg_generator_source(num=1)
    filterx = config.create_filterx(r"""
        $MSG = {
          "null": null,
          "nullidontwant":?? null,
          "erroridontwant":?? nonexistingvar,
          "value":?? 3,
        };
""")
    destination = config.create_file_destination(file_name="output.log", template='"$MSG\n"')

    config.create_logpath(statements=[source, filterx, destination])

    syslog_ng.start(config)
    assert destination.read_log() == '{"null":null,"value":3}'


def test_filterx_dpath(syslog_ng, config):
    source = config.create_example_msg_generator_source(num=1)
    filterx = config.create_filterx(r"""
        exist = {"orig": 1};
        dpath(exist.path.to.create) = {"value": {"a": 1}};
        newdict = {};
        dpath(newdict.path.to.create) = {"value": 3};
        dpath(newdict.path.to.create) += {"another": 4};
        d = {};
        dpath(d.exist) = exist;
        d.newdict = newdict;
        $MSG = d;
""")
    destination = config.create_file_destination(file_name="output.log", template='"$MSG\n"')

    config.create_logpath(statements=[source, filterx, destination])

    syslog_ng.start(config)
    assert destination.read_log() == '{"exist":{"orig":1,"path":{"to":{"create":{"value":{"a":1}}}}},"newdict":{"path":{"to":{"create":{"value":3,"another":4}}}}}'
