#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
# Copyright (c) 2015-2018 Balabit
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
import argparse
import logging
import os
import re
import uuid
from datetime import datetime
from pathlib import Path

import axosyslog_light.testcase_parameters.testcase_parameters as tc_parameters
import psutil
import pytest
import xdist
from axosyslog_light.common.file import copy_file
from axosyslog_light.common.pytest_operations import calculate_testcase_name
from axosyslog_light.common.session_data import get_session_data
from axosyslog_light.common.session_data import SessionData
from axosyslog_light.helpers.clickhouse.clickhouse_server_executor import ClickhouseServerExecutor
from axosyslog_light.helpers.dqtool.dqtool import DqTool
from axosyslog_light.helpers.dqtool.dqtool_docker_executor import DqToolDockerExecutor
from axosyslog_light.helpers.dqtool.dqtool_executor import DqToolExecutor
from axosyslog_light.helpers.dqtool.dqtool_local_executor import DqToolLocalExecutor
from axosyslog_light.helpers.loggen.loggen import Loggen
from axosyslog_light.helpers.loggen.loggen_docker_executor import LoggenDockerExecutor
from axosyslog_light.helpers.loggen.loggen_executor import LoggenExecutor
from axosyslog_light.helpers.loggen.loggen_local_executor import LoggenLocalExecutor
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


class InstallDirAction(argparse.Action):
    def __call__(self, parser, namespace, path, option_string=None):
        install_dir = Path(path)

        if not install_dir.is_dir():
            raise argparse.ArgumentTypeError("{0} is not a valid directory".format(path))

        binary = Path(install_dir, "sbin/syslog-ng")
        if not binary.exists():
            raise argparse.ArgumentTypeError("{0} not exist".format(binary))

        setattr(namespace, self.dest, path)


# Command line options
def pytest_addoption(parser):
    parser.addoption("--runslow", action="store_true", default=False, help="run slow tests")
    parser.addoption("--run-under", help="Run syslog-ng under selected tool, example tools: [valgrind, strace, gdb, gdb_for_bt]")

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
        help="Path for report files folder. Default form: 'reports/<current_date>'",
    )


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
    return parameters


@pytest.fixture
def config(request, syslog_ng, teardown) -> SyslogNgConfig:
    return syslog_ng.create_config(request.getfixturevalue("config_version"), teardown)


@pytest.fixture
def syslog_ng(request: pytest.FixtureRequest, testcase_parameters: TestcaseParameters, syslog_ng_ctl: SyslogNgCtl, container_name: str, teardown):
    if request.config.getoption("--runner") == "local":
        executor = SyslogNgLocalExecutor(tc_parameters.INSTANCE_PATH.get_syslog_ng_bin(), testcase_parameters)
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
    container_name = f"{testcase_parameters.get_testcase_name()}_{tc_parameters.INSTANCE_PATH.get_instance_name()}"
    return re.sub(r'[^a-zA-Z0-9_.-]', '_', container_name)


@pytest.fixture
def bsd_formatter():
    return BSDFormat()


@pytest.fixture
def log_message():
    return LogMessage()


@pytest.fixture
def version(syslog_ng):
    return syslog_ng.get_version()


@pytest.fixture
def config_version(syslog_ng):
    return syslog_ng.get_config_version()


@pytest.fixture
def loggen(testcase_parameters):
    server = Loggen()
    yield server
    server.stop()


@pytest.fixture
def dqtool(testcase_parameters):
    instance = DqTool()
    yield instance


@pytest.fixture
def clickhouse_server(request, testcase_parameters):
    clickhouse_server_instance = ClickhouseServerExecutor(testcase_parameters)
    yield clickhouse_server_instance
    if clickhouse_server_instance.process is not None:
        clickhouse_server_instance.stop()


def calculate_report_file_path(working_dir):
    return Path(working_dir, "testcase.reportlog")


def chdir_to_light_base_dir():
    absolute_light_base_dir = Path(__file__).parents[2]
    os.chdir(absolute_light_base_dir)


def __calculate_testcase_dir(name):
    with get_session_data() as session_data:
        return Path(session_data["reports_dir"], calculate_testcase_name(name))


@pytest.fixture
def testcase_dir(request):
    return __calculate_testcase_dir(request.node.name)


def pytest_runtest_setup(item):
    logging_plugin = item.config.pluginmanager.get_plugin("logging-plugin")
    testcase_dir = __calculate_testcase_dir(item.name)
    logging_plugin.set_log_path(calculate_report_file_path(testcase_dir))
    os.chdir(testcase_dir)


def pytest_sessionstart(session):
    if xdist.is_xdist_controller(session):
        return

    base_number_of_open_fds = len(psutil.Process().open_files())

    with get_session_data() as session_data:
        testrunuid = os.environ.get("PYTEST_XDIST_TESTRUNUID", uuid.uuid4().hex)  # generate one if not running in xdist
        if session_data.get("testrunuid") != testrunuid:
            session_data.clear()
            session_data["testrunuid"] = testrunuid

        session_data["active_workers"] = session_data.get("active_workers", 0) + 1

        if session_data.get("session_started", False):
            return

        try:
            reports_dir = Path(session.config.getoption("--reports")).resolve().absolute()
        except TypeError:
            reports_dir = Path("reports", get_current_date()).resolve().absolute()

        reports_dir.mkdir(parents=True, exist_ok=True)
        if reports_dir.parent.name == "reports":
            last_reports_dir = Path(reports_dir.parent, "last")
            last_reports_dir.unlink(True)
            last_reports_dir.symlink_to(reports_dir)

        session_data["session_started"] = True
        session_data["reports_dir"] = reports_dir
        session_data["base_number_of_open_fds"] = base_number_of_open_fds


def pytest_sessionfinish(session, exitstatus):
    if xdist.is_xdist_controller(session):
        return

    with get_session_data() as session_data:
        active_workers = session_data["active_workers"] = session_data["active_workers"] - 1

    if active_workers == 0:
        SessionData.cleanup()


def light_extra_files(target_dir):
    if "LIGHT_EXTRA_FILES" in os.environ:
        for f in os.environ["LIGHT_EXTRA_FILES"].split(":"):
            if Path(f).exists():
                copy_file(f, target_dir)


@pytest.fixture(autouse=True)
def setup(request):
    with get_session_data() as session_data:
        base_number_of_open_fds = session_data["base_number_of_open_fds"]
    number_of_open_fds = len(psutil.Process().open_files())
    if request.config.getoption("--run-under") != "valgrind":
        assert base_number_of_open_fds + 1 == number_of_open_fds, "Previous testcase has unclosed opened fds"
    for net_conn in psutil.Process().net_connections(kind="inet"):
        if net_conn.status == "CLOSE_WAIT":
            # This is a workaround for clickhouse-connect not closing connections properly
            continue
        if request.config.getoption("--run-under") != "valgrind":
            assert False, "Previous testcase has unclosed opened sockets: {}".format(net_conn)
    testcase_parameters = request.getfixturevalue("testcase_parameters")

    copy_file(testcase_parameters.get_testcase_file(), Path.cwd())
    light_extra_files(Path.cwd())
    request.addfinalizer(lambda: logger.info("Report file path\n{}\n".format(calculate_report_file_path(Path.cwd()))))

    if request.config.getoption("--runner") == "docker":
        LoggenExecutor.set_default_executor(
            LoggenDockerExecutor(request.config.getoption("--docker-image")),
        )
        DqToolExecutor.set_default_executor(
            DqToolDockerExecutor(request.config.getoption("--docker-image")),
        )
    elif request.config.getoption("--runner") == "local":
        LoggenExecutor.set_default_executor(
            LoggenLocalExecutor(Path(request.config.getoption("--installdir"), "bin", "loggen")),
        )
        DqToolExecutor.set_default_executor(
            DqToolLocalExecutor(Path(request.config.getoption("--installdir"), "bin", "dqtool")),
        )


@pytest.fixture
def port_allocator():
    def get_next_port() -> int:
        with get_session_data() as session_data:
            last_port = session_data.get("port_allocator_last_port", 20000)
            port = last_port + 1
            session_data["port_allocator_last_port"] = port
        return port

    return get_next_port


@pytest.fixture
def clickhouse_ports(port_allocator):
    class ClickhousePorts:
        def __init__(self):
            self.http_port = port_allocator()
            self.grpc_port = port_allocator()

    return ClickhousePorts()
