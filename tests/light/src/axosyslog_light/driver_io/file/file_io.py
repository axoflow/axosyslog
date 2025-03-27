#!/usr/bin/env python
#############################################################################
# Copyright (c) 2015-2018 Balabit
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
from axosyslog_light.common.file import File


class FileIO():
    def __init__(self, file_path):
        self.__readable_file = File(file_path)
        self.__writeable_file = File(file_path)

    def read_number_of_messages(self, counter):
        if not self.__readable_file.is_opened():
            if not self.__readable_file.wait_for_creation():
                raise Exception("{} was not created in time.".format(self.__readable_file.path))
            self.__readable_file.open("r")

        return [line.rstrip("\n") for line in self.__readable_file.wait_for_number_of_lines(counter)]

    def read_until_messages(self, lines):
        if not self.__readable_file.is_opened():
            if not self.__readable_file.wait_for_creation():
                raise Exception("{} was not created in time.".format(self.__readable_file.path))
            self.__readable_file.open("r")

        return [line.rstrip("\n") for line in self.__readable_file.wait_for_lines(lines)]

    def read_all(self):
        if not self.__readable_file.is_opened():
            if not self.__readable_file.wait_for_creation():
                raise Exception("{} was not created in time.".format(self.__readable_file.path))
            self.__readable_file.open("r")

        return self.__readable_file.read().splitlines()

    def write_raw(self, raw_content):
        if not self.__writeable_file.is_opened():
            self.__writeable_file.open("a+")

        self.__writeable_file.write_content_and_close(raw_content)

    def write_message(self, message):
        self.write_raw(message + "\n")

    def write_messages(self, messages):
        self.write_raw("".join([message + "\n" for message in messages]))

    def close_readable_file(self):
        self.__readable_file.close()

    def close_writeable_file(self):
        self.__writeable_file.close()
