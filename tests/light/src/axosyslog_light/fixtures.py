#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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
import argparse
import logging
import os
import re
import subprocess
from datetime import datetime
from pathlib import Path

import axosyslog_light.testcase_parameters.testcase_parameters as tc_parameters
import psutil
import pytest
from axosyslog_light.common.file import copy_file
from axosyslog_light.common.pytest_operations import calculate_testcase_name
from axosyslog_light.helpers.loggen.loggen import Loggen
from axosyslog_light.message_builder.bsd_format import BSDFormat
from axosyslog_light.message_builder.log_message import LogMessage
from axosyslog_light.syslog_ng.syslog_ng import SyslogNg
from axosyslog_light.syslog_ng.syslog_ng_docker_executor import SyslogNgDockerExecutor
from axosyslog_light.syslog_ng.syslog_ng_local_executor import SyslogNgLocalExecutor
from axosyslog_light.syslog_ng.syslog_ng_paths import SyslogNgPaths
from axosyslog_light.syslog_ng_config.syslog_ng_config import SyslogNgConfig
from axosyslog_light.syslog_ng_ctl.syslog_ng_ctl import SyslogNgCtl
from axosyslog_light.syslog_ng_ctl.syslog_ng_ctl_docker_executor import SyslogNgCtlDockerExecutor
from axosyslog_light.syslog_ng_ctl.syslog_ng_ctl_local_executor import SyslogNgCtlLocalExecutor
from axosyslog_light.testcase_parameters.testcase_parameters import TestcaseParameters

logger = logging.getLogger(__name__)
base_number_of_open_fds = 0


class InstallDirAction(argparse.Action):
    def __call__(self, parser, namespace, path, option_string=None):
        install_dir = Path(path)

        if not install_dir.is_dir():
            raise argparse.ArgumentTypeError("{0} is not a valid directory".format(path))

        binary = Path(install_dir, "sbin/syslog-ng")
        if namespace.runner == "local" and not binary.exists():
            raise argparse.ArgumentTypeError("{0} not exist".format(binary))

        setattr(namespace, self.dest, path)


# Command line options
def pytest_addoption(parser):
    parser.addoption("--runslow", action="store_true", default=False, help="run slow tests")
    parser.addoption("--run-under", help="Run syslog-ng under selected tool, example tools: [valgrind, strace]")

    parser.addoption(
        "--runner",
        default="local",
        choices=["local", "docker"],
        help="How to run AxoSyslog.",
    )
    parser.addoption(
        "--installdir",
        action=InstallDirAction,
        help="Look for AxoSyslog binaries here. Used when 'runner' is 'local'. Example path: '/home/user/axosyslog/install/'",
    )
    parser.addoption(
        "--docker-image",
        default="ghcr.io/axoflow/axosyslog:latest",
        help="Docker image to use for running syslog-ng. Used when 'runner' is 'docker'. Default: ghcr.io/axoflow/axosyslog:latest",
    )

    parser.addoption(
        "--reports",
        action="store",
        default=get_relative_report_dir(),
        help="Path for report files folder. Default form: 'reports/<current_date>'",
    )


def get_relative_report_dir():
    return str(Path("reports/", get_current_date()))


def get_current_date():
    return datetime.now().strftime("%Y-%m-%d-%H-%M-%S-%f")


# Pytest Hooks
def pytest_collection_modifyitems(config, items):
    if config.getoption("--runslow"):
        return
    skip_slow = pytest.mark.skip(reason="need --runslow option to run")
    for item in items:
        if "slow" in item.keywords:
            item.add_marker(skip_slow)


def pytest_runtest_logreport(report):
    if report.outcome == "failed":
        logger.error("\n{}".format(report.longrepr))


# Pytest Fixtures
@pytest.fixture
def testcase_parameters(request):
    parameters = TestcaseParameters(request)
    tc_parameters.INSTANCE_PATH = SyslogNgPaths(parameters).set_syslog_ng_paths("server")
    tc_parameters.RUNNER = request.config.getoption("--runner")
    return parameters


@pytest.fixture
def config(request, syslog_ng, teardown) -> SyslogNgConfig:
    return syslog_ng.create_config(request.getfixturevalue("version"), teardown)


@pytest.fixture
def syslog_ng(request: pytest.FixtureRequest, testcase_parameters: TestcaseParameters, syslog_ng_ctl: SyslogNgCtl, container_name: str, teardown):
    if request.config.getoption("--runner") == "local":
        executor = SyslogNgLocalExecutor(tc_parameters.INSTANCE_PATH.get_syslog_ng_bin())
    elif request.config.getoption("--runner") == "docker":
        executor = SyslogNgDockerExecutor(container_name, request.config.getoption("--docker-image"))
    else:
        raise ValueError("Invalid runner")

    syslog_ng = SyslogNg(executor, syslog_ng_ctl, tc_parameters.INSTANCE_PATH, testcase_parameters, teardown)
    teardown.register(syslog_ng.stop)
    return syslog_ng


class TeardownRegistry:
    teardown_callbacks = []

    def register(self, teardown_callback):
        TeardownRegistry.teardown_callbacks.append(teardown_callback)

    def execute_teardown_callbacks(self):
        for teardown_callback in TeardownRegistry.teardown_callbacks:
            teardown_callback()


@pytest.fixture(scope="function")
def teardown():
    teardown_registry = TeardownRegistry()
    yield teardown_registry
    teardown_registry.execute_teardown_callbacks()


@pytest.fixture
def syslog_ng_ctl(request: pytest.FixtureRequest, testcase_parameters, container_name):
    if request.config.getoption("--runner") == "local":
        executor = SyslogNgCtlLocalExecutor(
            tc_parameters.INSTANCE_PATH.get_syslog_ng_ctl_bin(),
            tc_parameters.INSTANCE_PATH.get_control_socket_path(),
        )
    elif request.config.getoption("--runner") == "docker":
        executor = SyslogNgCtlDockerExecutor(container_name)
    else:
        raise ValueError("Invalid runner")

    return SyslogNgCtl(tc_parameters.INSTANCE_PATH, executor)


@pytest.fixture
def container_name(request: pytest.FixtureRequest, testcase_parameters: TestcaseParameters):
    testcase_name_and_instance = f"{testcase_parameters.get_testcase_name()}_{tc_parameters.INSTANCE_PATH.get_instance_name()}"
    tc_parameters.CONTAINER_NAME = re.sub(r'[^a-zA-Z0-9_.-]', '_', testcase_name_and_instance)
    return tc_parameters.CONTAINER_NAME


@pytest.fixture
def bsd_formatter():
    return BSDFormat()


@pytest.fixture
def log_message():
    return LogMessage()


@pytest.fixture(scope="session")
def version(request):
    installdir = request.config.getoption("--installdir")
    binary_path = str(Path(installdir, "sbin", "syslog-ng"))
    version_output = subprocess.check_output([binary_path, "--version"]).decode()
    return version_output.splitlines()[1].split()[2]


@pytest.fixture
def loggen(testcase_parameters):
    server = Loggen()
    yield server
    server.stop()


def calculate_report_file_path(working_dir):
    return Path(working_dir, "testcase.reportlog")


def chdir_to_light_base_dir():
    absolute_light_base_dir = Path(__file__).parents[2]
    os.chdir(absolute_light_base_dir)


def get_report_dir(pytest_config_object):
    chdir_to_light_base_dir()
    return Path(pytest_config_object.getoption("--reports")).resolve().absolute()


def calculate_working_dir(pytest_config_object, testcase_name):
    report_dir = get_report_dir(pytest_config_object)
    return Path(report_dir, calculate_testcase_name(testcase_name))


def pytest_runtest_setup(item):
    logging_plugin = item.config.pluginmanager.get_plugin("logging-plugin")
    working_dir = calculate_working_dir(item.config, item.name)
    logging_plugin.set_log_path(calculate_report_file_path(working_dir))
    os.chdir(working_dir)


def pytest_sessionstart(session):
    global base_number_of_open_fds
    base_number_of_open_fds = len(psutil.Process().open_files())

    report_dir = get_report_dir(session.config)
    report_dir.mkdir(parents=True, exist_ok=True)
    if report_dir.parent.name == "reports":
        last_report_dir = Path(report_dir.parent, "last")
        last_report_dir.unlink(True)
        last_report_dir.symlink_to(report_dir)


def light_extra_files(target_dir):
    if "LIGHT_EXTRA_FILES" in os.environ:
        for f in os.environ["LIGHT_EXTRA_FILES"].split(":"):
            if Path(f).exists():
                copy_file(f, target_dir)


@pytest.fixture(autouse=True)
def setup(request):
    global base_number_of_open_fds
    number_of_open_fds = len(psutil.Process().open_files())
    assert base_number_of_open_fds + 1 == number_of_open_fds, "Previous testcase has unclosed opened fds"
    assert len(psutil.Process().net_connections(kind="inet")) == 0, "Previous testcase has unclosed opened sockets"
    testcase_parameters = request.getfixturevalue("testcase_parameters")

    copy_file(testcase_parameters.get_testcase_file(), Path.cwd())
    light_extra_files(Path.cwd())
    request.addfinalizer(lambda: logger.info("Report file path\n{}\n".format(calculate_report_file_path(Path.cwd()))))


class PortAllocator():
    CURRENT_DYNAMIC_PORT = 30000


@pytest.fixture(scope="session")
def port_allocator():
    def get_next_port():
        PortAllocator.CURRENT_DYNAMIC_PORT += 1
        return PortAllocator.CURRENT_DYNAMIC_PORT

    return get_next_port
