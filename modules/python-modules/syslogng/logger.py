#############################################################################
# Copyright (c) 2022 Balazs Scheidler <bazsi77@gmail.com>
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
# pylint: disable=unused-import,invalid-name

import logging

try:
    from _syslogng import Logger

    FAKE_LOGGER = False
except ImportError:
    import warnings
    warnings.warn("You have imported the syslogng package outside of syslog-ng, "
                  "thus some of the functionality is not available. "
                  "Defining fake classes for those exported by the underlying syslog-ng code")

    FAKE_LOGGER = True

    def Logger():
        return logging.getLogger()
    logging.basicConfig()


class SyslogNGInternalLogHandler(logging.Handler):
    syslogng_logger = Logger()

    def emit(self, record):

        msg = self.format(record)
        if record.levelno >= logging.ERROR:
            self.syslogng_logger.error(msg)
        elif record.levelno >= logging.WARNING:
            self.syslogng_logger.warning(msg)
        elif record.levelno >= logging.INFO:
            self.syslogng_logger.info(msg)
        elif record.levelno >= logging.DEBUG:
            self.syslogng_logger.debug(msg)
        else:
            self.syslogng_logger.trace(msg)


def setup_logging():
    logger = logging.getLogger()
    logger.setLevel(logging.DEBUG)
    sh = SyslogNGInternalLogHandler()
    formatter = logging.Formatter('python-%(name)s: %(message)s')
    sh.setFormatter(formatter)
    logger.addHandler(sh)


if not FAKE_LOGGER:
    setup_logging()
